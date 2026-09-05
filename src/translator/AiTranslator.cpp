#include "AiTranslator.h"
#include "../core/ProcessRunner.h"
#include "../core/ApiKeyPool.h"
#include "../core/Config.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <future>
#include <map>
#include "../../include/json.hpp"

namespace fs = std::filesystem;

namespace VideoDubber {

std::string AiTranslator::getLanguageName(const std::string& code) {
    if (code == "vi") return "Tiếng Việt";
    if (code == "en") return "English";
    if (code == "zh") return "Chinese";
    if (code == "ja") return "Japanese";
    if (code == "ko") return "Korean";
    if (code == "th") return "Thai";
    return code;
}

static std::vector<SrtItem> translateChunkViaScript(
    const std::vector<SrtItem>& chunk,
    const std::string& api_key,
    const std::string& provider,
    const std::string& target_language,
    const std::string& temp_dir,
    size_t chunk_idx,
    const std::vector<SrtItem>& prev_context
) {
    std::string chunk_in = temp_dir + "/trans_chunk_" + std::to_string(chunk_idx) + "_in.json";
    std::string chunk_out = temp_dir + "/trans_chunk_" + std::to_string(chunk_idx) + ".json";
    std::string chunk_ctx = temp_dir + "/trans_chunk_" + std::to_string(chunk_idx) + "_ctx.json";

    // Write chunk items
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& it : chunk) {
        arr.push_back({
            {"id", it.id},
            {"start_time", it.start_time},
            {"end_time", it.end_time},
            {"text", it.text},
            {"translated_text", it.translated_text},
            {"speaker", it.speaker.empty() ? "female" : it.speaker}
        });
    }
    {
        std::ofstream jf(chunk_in);
        if (jf.is_open()) jf << arr.dump(2);
    }

    // Write context items if any
    bool has_ctx = !prev_context.empty();
    if (has_ctx) {
        nlohmann::json ctx_arr = nlohmann::json::array();
        for (const auto& it : prev_context) {
            ctx_arr.push_back({
                {"id", it.id},
                {"text", it.text},
                {"translated_text", it.translated_text}
            });
        }
        std::ofstream cjf(chunk_ctx);
        if (cjf.is_open()) cjf << ctx_arr.dump(2);
    }

    std::string cmd = "python gemini_script_rewriter.py \"" + chunk_in + "\" \"" + target_language + "\" \"\" \"" + api_key + "\" \"" + provider + "\" --out-file \"" + chunk_out + "\"";
    if (has_ctx) {
        cmd += " --context-file \"" + chunk_ctx + "\"";
    }

    int ret = ProcessRunner::execute(cmd);

    std::vector<SrtItem> result_items = chunk;
    if (ret == 0 && fs::exists(fs::u8path(chunk_out))) {
        try {
            std::ifstream out_jf(chunk_out);
            nlohmann::json trans_arr;
            out_jf >> trans_arr;
            if (trans_arr.is_array()) {
                std::map<int, nlohmann::json> id_map;
                for (const auto& item_j : trans_arr) {
                    if (item_j.is_object() && item_j.contains("id")) {
                        id_map[item_j["id"].get<int>()] = item_j;
                    }
                }
                for (auto& item : result_items) {
                    auto it = id_map.find(item.id);
                    if (it != id_map.end()) {
                        std::string trans = it->second.value("translated_text", "");
                        if (trans.empty()) trans = it->second.value("vietnamese", "");
                        if (!trans.empty()) {
                            item.translated_text = trans;
                        }
                        item.speaker = it->second.value("speaker", "female");
                    }
                }
            }
            ApiKeyPool::instance().reportSuccess(provider, api_key);
        } catch (const std::exception& e) {
            std::cerr << "[AiTranslator WARN] Parse chunk " << chunk_idx << " failed: " << e.what() << "\n";
            ApiKeyPool::instance().reportError(provider, api_key, 500);
        }
    } else {
        std::cerr << "[AiTranslator WARN] Chunk " << chunk_idx << " execution failed. Ret: " << ret << "\n";
        ApiKeyPool::instance().reportError(provider, api_key, 500);
    }

    return result_items;
}

TranslationResult AiTranslator::translateSrt(
    const std::vector<SrtItem>& items,
    const std::string& target_language,
    const std::string& provider,
    const std::string& temp_dir,
    std::function<void(int, const std::string&)> on_progress
) {
    TranslationResult result;
    if (items.empty()) {
        result.success = true;
        return result;
    }

    result.translated_items = items;
    std::string lang_name = getLanguageName(target_language);
    std::string prov = (!provider.empty()) ? provider : "gemini";
    std::string prov_name = (prov == "deepseek") ? "DeepSeek AI" : "Gemini AI";

    if (on_progress) on_progress(15, "Đang khởi tạo " + prov_name + " (" + lang_name + ")...");
    fs::create_directories(fs::u8path(temp_dir));

    auto cfg = ConfigManager::instance().getConfig();
    size_t chunk_size = (cfg.translation_chunk_size > 0) ? cfg.translation_chunk_size : 40;
    size_t num_chunks = (items.size() + chunk_size - 1) / chunk_size;

    std::vector<std::future<std::vector<SrtItem>>> futures;
    std::vector<SrtItem> prev_context;

    for (size_t c = 0; c < num_chunks; ++c) {
        size_t start_idx = c * chunk_size;
        size_t end_idx = std::min(items.size(), start_idx + chunk_size);
        std::vector<SrtItem> chunk(items.begin() + start_idx, items.begin() + end_idx);

        std::string key = ApiKeyPool::instance().getNextKey(prov);

        futures.push_back(std::async(std::launch::async, translateChunkViaScript,
            chunk, key, prov, target_language, temp_dir, c, prev_context));

        // Cập nhật context là 2-3 dòng cuối của chunk vừa dispatch cho chunk tiếp theo
        prev_context.clear();
        size_t ctx_count = std::min<size_t>(3, chunk.size());
        for (size_t k = chunk.size() - ctx_count; k < chunk.size(); ++k) {
            prev_context.push_back(chunk[k]);
        }
    }

    if (on_progress) on_progress(30, prov_name + " đang dịch song song " + std::to_string(num_chunks) + " chunks qua nhiều API keys...");

    // Collect results strictly mapped by ID
    std::map<int, SrtItem> id_to_translated;
    for (size_t c = 0; c < futures.size(); ++c) {
        try {
            auto part = futures[c].get();
            for (const auto& it : part) {
                id_to_translated[it.id] = it;
            }
        } catch (const std::exception& e) {
            std::cerr << "[AiTranslator ERROR] Chunk " << c << " future exception: " << e.what() << "\n";
        }
        if (on_progress) {
            int p = static_cast<int>((c + 1) * 60 / futures.size()) + 30;
            on_progress(p, "Hoàn tất chunk " + std::to_string(c + 1) + "/" + std::to_string(futures.size()) + "...");
        }
    }

    // Merge PHẢI theo "id" gắn sẵn trong JSON của từng dòng, KHÔNG merge theo thứ tự mảng —
    // nếu model trả về số dòng không khớp số dòng gửi, giữ nguyên bản gốc cho dòng đó, không lệch timeline các dòng sau.
    for (auto& item : result.translated_items) {
        auto it = id_to_translated.find(item.id);
        if (it != id_to_translated.end() && !it->second.translated_text.empty()) {
            item.translated_text = it->second.translated_text;
            item.speaker = it->second.speaker;
        } else {
            if (item.translated_text.empty()) {
                item.translated_text = item.text;
            }
        }
    }

    // Export final unified JSON & SRT
    std::string temp_json = temp_dir + "/trans_input.json";
    std::string temp_srt = temp_dir + "/trans_output.srt";
    {
        nlohmann::json final_arr = nlohmann::json::array();
        for (const auto& it : result.translated_items) {
            final_arr.push_back({
                {"id", it.id},
                {"start_time", it.start_time},
                {"end_time", it.end_time},
                {"text", it.text},
                {"translated_text", it.translated_text},
                {"speaker", it.speaker}
            });
        }
        std::ofstream jf(temp_json);
        if (jf.is_open()) jf << final_arr.dump(2);
    }
    SrtParser::saveToFile(temp_srt, result.translated_items, true);

    result.success = true;
    if (on_progress) on_progress(100, "Hoàn tất kịch bản lồng tiếng " + std::to_string(items.size()) + " câu thoại");
    return result;
}

} // namespace VideoDubber
