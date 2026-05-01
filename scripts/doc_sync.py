#!/usr/bin/env python3
"""
Simple script to ensure basic consistency between documentation and code.
It checks for the presence of key classes/functions in the source code
that are mentioned in the architecture documentation.
"""

import os
import re
import sys

DOC_FILE = "docs/architecture.mmd"
SRC_DIR = "src"

REQUIRED_COMPONENTS = [
    "VoicebotEndpoint",
    "VoicebotAccount",
    "VoicebotCall",
    "VoicebotMediaPort",
    "VoicebotAiClient",
    "SessionManager"
]

def check_component_exists(component):
    for root, _, files in os.walk(SRC_DIR):
        for file in files:
            if file.endswith((".cpp", ".h")):
                with open(os.path.join(root, file), 'r') as f:
                    content = f.read()
                    if component in content:
                        return True
    return False

def main():
    print("Checking code consistency against documentation...")
    missing = []
    for comp in REQUIRED_COMPONENTS:
        if check_component_exists(comp):
            print(f"✅ Found {comp} in source code.")
        else:
            print(f"❌ Missing {comp} in source code.")
            missing.append(comp)
            
    if missing:
        print(f"\nError: {len(missing)} components documented but missing in code.")
        sys.exit(1)
    
    print("\nDocumentation and code consistency check passed!")

if __name__ == "__main__":
    main()
