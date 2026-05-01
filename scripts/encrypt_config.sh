#!/bin/bash
# SOPS Encryption/Decryption Helper Script

set -e

usage() {
    echo "Usage: $0 [encrypt|decrypt] [file_path]"
    echo "Example: $0 encrypt config/session.yaml"
    exit 1
}

if [ $# -ne 2 ]; then
    usage
fi

ACTION=$1
FILE_PATH=$2

if [ ! -f "$FILE_PATH" ]; then
    echo "Error: File $FILE_PATH does not exist."
    exit 1
fi

# Ensure sops is installed
if ! command -v sops &> /dev/null; then
    echo "Error: sops is not installed. Please install it first."
    exit 1
fi

if [ "$ACTION" == "encrypt" ]; then
    echo "Encrypting $FILE_PATH..."
    # Encrypt in place using age (requires SOPS_AGE_KEY_FILE environment variable)
    if [ -z "$SOPS_AGE_KEY_FILE" ]; then
        echo "Warning: SOPS_AGE_KEY_FILE is not set. Assuming KMS or PGP is configured."
    fi
    sops -i -e "$FILE_PATH"
    echo "Encryption complete."

elif [ "$ACTION" == "decrypt" ]; then
    echo "Decrypting $FILE_PATH..."
    sops -i -d "$FILE_PATH"
    echo "Decryption complete."
else
    usage
fi
