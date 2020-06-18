#!/bin/sh -ex

pushd ~/software/emsdk/
source ./emsdk_env.sh
popd

./build os=emscripten arch=wasm

mkdir -p out/web
cp out/ReleaseEmscriptenWASM/primordialsoup.* out/web
cp out/snapshots/*.vfuel out/web
cp newspeak/*.ns newspeak/*.png out/web
cp -R CodeMirror out/web

pushd out/web
../ReleaseX64/primordialsoup \
    ../snapshots/WebCompiler.vfuel \
    *.ns \
    *.png \
    CodeMirror/lib/codemirror.js \
    CodeMirror/addon/display/autorefresh.js \
    RuntimeWithMirrorsForPrimordialSoup HopscotchWebIDE HopscotchWebIDE.vfuel \
    RuntimeWithMirrorsForPrimordialSoup Ampleforth Ampleforth.vfuel \
popd

echo "Visit http://localhost:8080/primordialsoup.html?snapshot=HopscotchWebIDE.vfuel"
pushd out/web
python ../../server.py
popd
