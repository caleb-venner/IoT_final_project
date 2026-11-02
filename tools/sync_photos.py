"""Synchronise captured JPEG images and capture_log.csv from the ESP32 HTTP server to a local directory."""

import argparse
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path


def fetch_json(url: str, timeout: float) -> dict:
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def download_file(url: str, destination: Path, timeout: float) -> int:
    with urllib.request.urlopen(url, timeout=timeout) as response:
        data = response.read()
    destination.write_bytes(data)
    return len(data)


def post_json(url: str, timeout: float) -> dict:
    req = urllib.request.Request(url, data=b"", method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def get_status(host: str, timeout: float) -> int:
    base_url = host if host.startswith("http") else f"http://{host}"
    base_url = base_url.rstrip("/")

    try:
        status = fetch_json(f"{base_url}/api/status", timeout=timeout)
    except urllib.error.URLError as exc:
        print(f"Failed to fetch status from {base_url}/api/status: {exc}", file=sys.stderr)
        return 1

    total_b = status.get("total_bytes", 0)
    used_b = status.get("used_bytes", 0)
    free_b = status.get("free_bytes", 0)
    total_mb = total_b / (1024 * 1024)
    used_mb = used_b / (1024 * 1024)
    free_mb = free_b / (1024 * 1024)
    pct = (used_b / total_b * 100.0) if total_b > 0 else 0.0
    est_photos = status.get("est_photos_remaining", 0)
    photo_count = status.get("photo_count", 0)
    uptime_s = status.get("uptime_s", 0)
    sensor = status.get("sensor", "unknown")

    print("=== ESP32 Basil Camera Status ===")
    print(f"Host:       {base_url}")
    print(f"Uptime:     {uptime_s} s")
    print(f"Sensor:     {sensor}")
    print(f"SD Mounted: {'Yes' if status.get('sd_mounted') else 'No'}")
    print(f"Storage:    {used_mb:.1f} MB used / {total_mb:.1f} MB total ({pct:.1f}% used)")
    print(f"Available:  {free_mb:.1f} MB free (~{est_photos:,} photos at ~15 KB each)")
    print(f"Photos:     {photo_count} on card (next file: img_{status.get('next_index', 1):05d}.jpg)")
    return 0


def sync_from_node(host: str, output_dir: Path, timeout: float, clear_after: bool = False, delete_log: bool = False) -> int:
    base_url = host if host.startswith("http") else f"http://{host}"
    base_url = base_url.rstrip("/")
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        manifest = fetch_json(f"{base_url}/api/files", timeout=timeout)
    except urllib.error.URLError as exc:
        print(f"Failed to reach {base_url}/api/files: {exc}", file=sys.stderr)
        return 1

    files = manifest.get("files", [])
    files.sort(key=lambda item: item.get("name", ""))
    total_b = manifest.get("total_bytes", 0)
    used_b = manifest.get("used_bytes", 0)
    if total_b > 0:
        used_mb = used_b / (1024 * 1024)
        total_mb = total_b / (1024 * 1024)
        print(f"Node SD Storage: {used_mb:.1f} MB / {total_mb:.1f} MB ({len(files)} files)")

    downloaded = 0
    skipped = 0
    errors = 0

    for entry in files:
        remote_name = entry.get("name", "")
        remote_size = int(entry.get("size", 0))
        if not remote_name:
            continue

        local_path = output_dir / remote_name.lstrip("/")
        # Always re-download capture_log.csv; skip JPEGs that already match size on disk
        if (
            local_path.suffix == ".jpg"
            and local_path.exists()
            and local_path.stat().st_size == remote_size
            and remote_size > 0
        ):
            skipped += 1
            continue

        file_url = f"{base_url}{remote_name}"
        try:
            written = download_file(file_url, local_path, timeout=timeout)
            print(f"Downloaded {local_path.name} ({written} bytes)")
            downloaded += 1
        except urllib.error.URLError as exc:
            print(f"Error downloading {file_url}: {exc}", file=sys.stderr)
            errors += 1

    print(f"Sync complete: {downloaded} downloaded, {skipped} already up to date in {output_dir}")

    if clear_after:
        if errors > 0:
            print(f"Warning: {errors} files failed to download. Aborting remote card clearing for safety.", file=sys.stderr)
            return 1

        clear_url = f"{base_url}/api/clear"
        if delete_log:
            clear_url += "?delete_log=1"

        print(f"Clearing photos on remote SD card ({clear_url})...")
        try:
            res = post_json(clear_url, timeout=timeout)
            deleted_count = res.get("deleted", 0)
            new_used_mb = res.get("used_bytes", 0) / (1024 * 1024)
            print(f"Remote SD card cleared: {deleted_count} files removed. Storage remaining used: {new_used_mb:.1f} MB.")
        except urllib.error.URLError as exc:
            print(f"Failed to clear SD card at {clear_url}: {exc}", file=sys.stderr)
            return 1

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Synchronise SD card captures from ESP32 over HTTP and check storage.")
    parser.add_argument(
        "--host",
        default="basil-cam.local",
        help="Hostname or IP of the ESP32 capture node (default: basil-cam.local)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("local/capture/downloads"),
        help="Destination folder for downloaded images (default: local/capture/downloads)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=10.0,
        help="HTTP request timeout in seconds (default: 10.0)",
    )
    parser.add_argument(
        "--status",
        action="store_true",
        help="Check and display SD card storage usage and capacity without downloading",
    )
    parser.add_argument(
        "--clear",
        action="store_true",
        help="Clear photos from remote SD card after successful download",
    )
    parser.add_argument(
        "--delete-log",
        action="store_true",
        help="When --clear is used, also delete capture_log.csv from the SD card",
    )
    args = parser.parse_args()

    if args.status:
        return get_status(args.host, args.timeout)

    return sync_from_node(args.host, args.out, args.timeout, clear_after=args.clear, delete_log=args.delete_log)


if __name__ == "__main__":
    raise SystemExit(main())
