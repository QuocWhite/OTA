#pragma once

// RSA-2048 public key (DER format) for firmware signature verification
// Run: python sign_update.py --genkey && python sign_update.py --embed
// This is a placeholder — replace with your generated key!
const uint8_t PUBLIC_KEY[] = {0x00};
const size_t PUBLIC_KEY_LEN = 1;
