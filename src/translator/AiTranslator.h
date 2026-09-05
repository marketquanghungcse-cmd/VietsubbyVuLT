#pragma once
#include <string>
#include <vector>
#include <functional>
#include "../stt/SrtParser.h"
#include "../core/ApiKeyPool.h"

namespace VideoDubber {

struct TranslationResult {
    bool success = false;
    std::vector<SrtItem> translated_items;
    std::string error;
};

class AiTranslator {
public:
    static TranslationResult translateSrt(
        const std::vector<SrtItem>& items,
        const std::string& target_language = "vi",
        const std::string& provider = "gemini",
        const std::string& temp_dir = "temp",
        std::function<void(int progress, const std::string& msg)> on_progress = nullptr
    );

private:
    static bool translateGemini(
        const std::vector<SrtItem>& chunk,
        const std::string& target_lang_name,
        std::vector<std::string>& out_translations,
        std::string& error_msg
    );

    static bool translateDeepSeek(
        const std::vector<SrtItem>& chunk,
        const std::string& target_lang_name,
        std::vector<std::string>& out_translations,
        std::string& error_msg
    );

    static std::string getLanguageName(const std::string& code);
};

} // namespace VideoDubber