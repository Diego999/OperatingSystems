#!/bin/bash

REFERENCE_COMMIT="1bce8c5c"

if [ $# -ne 1 ]; then
    echo "Usage: $0 PATCH_FILE_NAME"
fi

if [ -f $1 ]; then
    echo "File $1 already exists. Quitting."
    exit 1
fi

pushd .
cd /usr/src/linux
make distclean
git checkout .config
git -c core.fileMode=false add -A
git commit -m "Student solution patch"
git -c core.fileMode=false diff $REFERENCE_COMMIT HEAD > $1
popd
mv /usr/src/linux/$1 .
