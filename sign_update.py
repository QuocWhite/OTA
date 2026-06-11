"""
Sign firmware binary and generate version manifest for GitHub OTA.

Usage:
    # First time: generate a signing key (keep private.key secret!)
    python sign_update.py --genkey

    # Each release: sign the firmware binary
    python sign_update.py firmware.bin --version 1.1

    # Upload to GitHub Releases:
    #   dist/v1.1/firmware.bin
    #   dist/v1.1/version.json
"""

import argparse
import hashlib
import json
import os
import subprocess
import sys

KEY_FILE = "private.key"
PUB_FILE = "public.key"
DIST_DIR = "dist"


def genkey():
    """Generate RSA-2048 key pair for firmware signing."""
    subprocess.run(
        ["openssl", "genrsa", "-out", KEY_FILE, "2048"],
        check=True,
    )
    subprocess.run(
        ["openssl", "rsa", "-in", KEY_FILE, "-pubout", "-out", PUB_FILE],
        check=True,
    )
    print(f"Generated {KEY_FILE} (private — KEEP SECRET)")
    print(f"Generated {PUB_FILE} (public — embed in firmware)")


def embed_pub_key():
    """Convert public key to C header for firmware."""
    result = subprocess.run(
        ["openssl", "rsa", "-in", KEY_FILE, "-pubout", "-outform", "DER"],
        capture_output=True,
        check=True,
    )
    der_data = result.stdout
    hex_bytes = ", ".join(f"0x{b:02x}" for b in der_data)
    header = f"""#pragma once

// RSA-2048 public key (DER format) for firmware signature verification
const uint8_t PUBLIC_KEY[] = {{{hex_bytes}}};
const size_t PUBLIC_KEY_LEN = {len(der_data)};
"""
    with open("src/public_key.h", "w") as f:
        f.write(header)
    print(f"Wrote src/public_key.h ({len(der_data)} bytes)")


def sign(bin_path, version):
    """Sign firmware binary and generate version manifest."""
    if not os.path.exists(KEY_FILE):
        print(f"Error: {KEY_FILE} not found. Run with --genkey first.")
        sys.exit(1)

    # Compute SHA256
    sha256 = hashlib.sha256()
    with open(bin_path, "rb") as f:
        sha256.update(f.read())
    hash_hex = sha256.hexdigest()

    # Sign the binary with RSA
    sig_path = bin_path + ".sig"
    subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", KEY_FILE, "-out", sig_path, bin_path],
        check=True,
    )

    # Read signature as hex
    with open(sig_path, "rb") as f:
        sig_hex = f.read().hex()

    # Create version manifest
    manifest = {
        "version": version,
        "sha256": hash_hex,
        "signature": sig_hex,
        "binary": f"firmware.bin",
    }

    # Write to dist directory
    version_dir = os.path.join(DIST_DIR, f"v{version}")
    os.makedirs(version_dir, exist_ok=True)

    manifest_path = os.path.join(version_dir, "version.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)

    # Copy binary and signature to dist
    import shutil
    shutil.copy(bin_path, os.path.join(version_dir, "firmware.bin"))
    shutil.copy(sig_path, os.path.join(version_dir, "firmware.bin.sig"))

    print(f"Signed firmware v{version}")
    print(f"  SHA256: {hash_hex}")
    print(f"  Manifest: {manifest_path}")
    print(f"  Upload dist/v{version}/ to GitHub Releases")


def main():
    parser = argparse.ArgumentParser(
        description="Sign firmware binary for GitHub OTA")
    parser.add_argument("binary", nargs="?", help="Path to firmware.bin")
    parser.add_argument("--version", "-v", default="1.0", help="Version string")
    parser.add_argument("--genkey", action="store_true", help="Generate signing key")
    parser.add_argument("--embed", action="store_true", help="Embed public key into firmware header")
    args = parser.parse_args()

    if args.genkey:
        genkey()
        return

    if args.embed:
        embed_pub_key()
        return

    if args.binary:
        sign(args.binary, args.version)
        return

    parser.print_help()


if __name__ == "__main__":
    main()
