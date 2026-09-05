import sys
import os
import json
import zipfile

def make_zip(manifest_path, output_zip):
    if not os.path.exists(manifest_path):
        print(f"[make_batch_zip] Error: Manifest not found: {manifest_path}", file=sys.stderr)
        sys.exit(1)

    try:
        with open(manifest_path, 'r', encoding='utf-8') as f:
            items = json.load(f)
    except Exception as e:
        print(f"[make_batch_zip] Error reading manifest: {e}", file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(os.path.abspath(output_zip)), exist_ok=True)
    temp_zip = output_zip + ".tmp"
    
    added_count = 0
    try:
        with zipfile.ZipFile(temp_zip, 'w', compression=zipfile.ZIP_STORED, allowZip64=True) as zf:
            used_arcnames = set()
            for item in items:
                src_path = item.get("path", "")
                arc_name = item.get("name", "")
                if not src_path or not os.path.exists(src_path):
                    continue
                if not arc_name:
                    arc_name = os.path.basename(src_path)

                # Ensure unique arcname in case duplicate names exist
                base, ext = os.path.splitext(arc_name)
                candidate = arc_name
                counter = 1
                while candidate.lower() in used_arcnames:
                    candidate = f"{base}_{counter}{ext}"
                    counter += 1
                used_arcnames.add(candidate.lower())

                zf.write(src_path, arcname=candidate)
                added_count += 1
                print(f"[make_batch_zip] Added: {src_path} -> {candidate}")

        if os.path.exists(output_zip):
            try:
                os.remove(output_zip)
            except Exception:
                pass
        os.rename(temp_zip, output_zip)
        print(f"[make_batch_zip] Successfully created {output_zip} with {added_count} files.")
        return 0
    except Exception as e:
        if os.path.exists(temp_zip):
            try:
                os.remove(temp_zip)
            except Exception:
                pass
        print(f"[make_batch_zip] Error creating zip: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python make_batch_zip.py <manifest_json> <output_zip>")
        sys.exit(1)
    manifest = sys.argv[1]
    out_zip = sys.argv[2]
    sys.exit(make_zip(manifest, out_zip))
