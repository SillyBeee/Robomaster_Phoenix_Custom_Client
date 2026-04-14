#!/bin/bash
# Generate protobuf files from include/protocol/protocol.proto
# Output: .h -> include/protocol/, .cc -> src/protocol/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROTO_FILE="$PROJECT_ROOT/include/protocol/protocol.proto"
PROTO_DIR="$PROJECT_ROOT/include/protocol"
OUT_H="$PROJECT_ROOT/include/protocol"
OUT_CC="$PROJECT_ROOT/src/protocol"

# Check protoc is available
if ! command -v protoc &>/dev/null; then
    echo "Error: protoc not found. Install libprotobuf-dev and protobuf-compiler." >&2
    exit 1
fi

# Check proto file exists
if [ ! -f "$PROTO_FILE" ]; then
    echo "Error: $PROTO_FILE not found." >&2
    exit 1
fi

mkdir -p "$OUT_H" "$OUT_CC"

# Generate into a temp dir so we can split .h and .cc
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

protoc \
    --proto_path="$PROTO_DIR" \
    --cpp_out="$TMP_DIR" \
    --experimental_allow_proto3_optional \
    "$(basename "$PROTO_FILE")"

PROTO_BASE="$(basename "$PROTO_FILE" .proto)"

# Move header
mv "$TMP_DIR/${PROTO_BASE}.pb.h" "$OUT_H/"

# Fix include path in .cc to point at include/protocol/
sed -i "s|#include \"${PROTO_BASE}.pb.h\"|#include \"protocol/${PROTO_BASE}.pb.h\"|g" \
    "$TMP_DIR/${PROTO_BASE}.pb.cc"

# Move source
mv "$TMP_DIR/${PROTO_BASE}.pb.cc" "$OUT_CC/"

echo "Generated:"
echo "  $OUT_H/${PROTO_BASE}.pb.h"
echo "  $OUT_CC/${PROTO_BASE}.pb.cc"
