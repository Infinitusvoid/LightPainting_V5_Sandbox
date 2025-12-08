import os
from datetime import datetime

# -------------------------------------------------------------------
# List of header files to ignore when searching for "last" .h file.
# -------------------------------------------------------------------
IGNORED_HEADERS = {
    "WireEngine.h",
    "WireEngine_v3.h",
    "WireEngine_v4.h",
    "make_new_version.h",   # if you ever have such a file, just an example
    # Add more names here if needed
}

def find_latest_header(folder):
    """Return path to the most recently modified .h file in folder,
    skipping anything in IGNORED_HEADERS. Returns None if nothing found."""
    candidates = []

    for name in os.listdir(folder):
        if not name.lower().endswith(".h"):
            continue
        if name in IGNORED_HEADERS:
            continue

        full = os.path.join(folder, name)
        if not os.path.isfile(full):
            continue

        mtime = os.path.getmtime(full)
        candidates.append((mtime, full))

    if not candidates:
        print("[ERROR] No .h files found (after applying ignore list).")
        return None

    candidates.sort(key=lambda x: x[0], reverse=True)
    latest_path = candidates[0][1]
    print(f"[INFO] Latest header: {os.path.basename(latest_path)}")
    return latest_path


def make_new_header_name():
    """Create a name like W_<day>_<month>_<year>_<hour>_<minute>.h"""
    now = datetime.now()
    return "W_{:02d}_{:02d}_{:04d}_{:02d}_{:02d}.h".format(
        now.day, now.month, now.year, now.hour, now.minute
    )


def main():
    folder = os.path.dirname(os.path.abspath(__file__))

    latest_header = find_latest_header(folder)
    if latest_header is None:
        raise SystemExit(1)

    new_name = make_new_header_name()
    new_path = os.path.join(folder, new_name)

    # --- COPY HEADER IN BINARY MODE (no encoding issues) ------------------
    with open(latest_header, "rb") as src, open(new_path, "wb") as dst:
        dst.write(src.read())

    print(f"[INFO] Created new header: {new_name}")

    # Overwrite main.cpp with a single include line
    main_cpp_path = os.path.join(folder, "main.cpp")
    include_line = f'#include "{new_name}"\n'

    # main.cpp we can safely write as UTF-8 text
    with open(main_cpp_path, "w", encoding="utf-8") as f:
        f.write(include_line)

    print(f"[INFO] main.cpp updated to include {new_name}")


if __name__ == "__main__":
    main()
