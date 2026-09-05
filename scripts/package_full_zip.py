import os, sys, zipfile, time

if sys.platform == 'win32':
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
        sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT_ZIP = os.path.join(os.path.dirname(ROOT_DIR), "VideoDubberPro_FullPackage.zip")

EXCLUDE_DIRS = {
    'temp', 'output', 'outputs', 'build', '.git', '__pycache__', '.vs', '.vscode'
}

EXCLUDE_EXTS = {
    '.zip', '.log', '.obj', '.tlog', '.idb', '.pdb', '.ilk'
}

print(f"Bắt đầu đóng gói VideoDubberPro từ: {ROOT_DIR}")
print(f"Đích đến: {OUTPUT_ZIP}")
start_time = time.time()

if os.path.exists(OUTPUT_ZIP):
    try:
        os.remove(OUTPUT_ZIP)
    except Exception as e:
        print(f"Không thể xóa zip cũ: {e}")

total_files = 0
total_bytes = 0

with zipfile.ZipFile(OUTPUT_ZIP, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
    for root, dirs, files in os.walk(ROOT_DIR):
        # Exclude directories
        dirs[:] = [d for d in dirs if d.lower() not in EXCLUDE_DIRS]
        
        for file in files:
            ext = os.path.splitext(file)[1].lower()
            if ext in EXCLUDE_EXTS:
                continue
            if file == 'package_full_zip.py':
                continue
            
            full_path = os.path.join(root, file)
            rel_path = os.path.relpath(full_path, ROOT_DIR)
            arcname = os.path.join("VideoDubberPro", rel_path)
            
            try:
                fsize = os.path.getsize(full_path)
                zf.write(full_path, arcname)
                total_files += 1
                total_bytes += fsize
                if total_files % 50 == 0:
                    print(f"  + Đã nén {total_files} tệp ({total_bytes / (1024*1024):.1f} MB uncompressed)...")
            except Exception as e:
                print(f"  ! Bỏ qua {rel_path}: {e}")

zip_size_mb = os.path.getsize(OUTPUT_ZIP) / (1024 * 1024)
elapsed = time.time() - start_time
print("=" * 60)
print(f"🎉 ĐÓNG GÓI HOÀN TẤT TRONG {elapsed:.1f} GIÂY!")
print(f"📦 Tổng số tệp: {total_files}")
print(f"💾 Dung lượng gốc: {total_bytes / (1024*1024):.2f} MB")
print(f"📁 Dung lượng File ZIP thành phẩm: {zip_size_mb:.2f} MB")
print(f"📍 Đường dẫn file zip: {OUTPUT_ZIP}")
print("=" * 60)
