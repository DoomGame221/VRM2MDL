#!/bin/bash
# Install AppImageTool
wget -O appimagetool https://github.com/AppImage/AppImageKit/releases/download/13/appimagetool-x86_64.AppImage
chmod +x appimagetool

# Create AppDir structure
mkdir -p AppDir/usr/bin
mkdir -p AppDir/usr/lib/vrm2mdl/cores

# Copy binaries
cp vrm_gui/target/release/vrm_gui AppDir/usr/bin/
cp build/bin/libvrm_core.so AppDir/usr/lib/vrm2mdl/cores/
cp build/bin/libassimp_core.so AppDir/usr/lib/vrm2mdl/cores/ || true

# Copy desktop file and icon
cp vrm2mdl.desktop AppDir/
if [ -f "icon.png" ]; then
    cp icon.png AppDir/vrm2mdl.png
else
    # Fallback to a dummy icon if not found
    touch AppDir/vrm2mdl.png
fi

# Create AppRun
cat > AppDir/AppRun << 'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_LIBRARY_PATH="${HERE}/usr/lib/vrm2mdl/cores:${LD_LIBRARY_PATH}"
exec "${HERE}/usr/bin/vrm_gui" "$@"
EOF
chmod +x AppDir/AppRun

# Build AppImage
./appimagetool AppDir VRM2MDL-x86_64.AppImage
