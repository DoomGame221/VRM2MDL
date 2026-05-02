#!/bin/bash
set -e

# Download appimagetool
wget -q https://github.com/AppImage/AppImageKit/releases/download/13/appimagetool-x86_64.AppImage -O appimagetool
chmod +x appimagetool

# Prepare AppDir
mkdir -p AppDir/usr/bin
mkdir -p AppDir/usr/lib/vrm2mdl/cores

# Copy binaries and libraries
cp vrm_gui/target/release/vrm_gui AppDir/usr/bin/
if [ -f "build/bin/libvrm_core.so" ]; then
    cp build/bin/libvrm_core.so AppDir/usr/lib/vrm2mdl/cores/
fi
if [ -f "build/bin/libassimp_core.so" ]; then
    cp build/bin/libassimp_core.so AppDir/usr/lib/vrm2mdl/cores/
fi

# Copy desktop and icon
cp vrm2mdl.desktop AppDir/
if [ -f "icon.png" ]; then
    cp icon.png AppDir/vrm2mdl.png
else
    # Create a 1x1 dummy png if icon is missing
    echo "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==" | base64 -d > AppDir/vrm2mdl.png
fi

# Create AppRun
cat > AppDir/AppRun << 'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_LIBRARY_PATH="${HERE}/usr/lib/vrm2mdl/cores:${LD_LIBRARY_PATH}"
export PATH="${HERE}/usr/bin:${PATH}"
exec vrm_gui "$@"
EOF
chmod +x AppDir/AppRun

# Generate AppImage
# APPIMAGE_EXTRACT_AND_RUN=1 is required for environments without FUSE (like GitHub Actions)
export APPIMAGE_EXTRACT_AND_RUN=1
./appimagetool AppDir VRM2MDL-x86_64.AppImage
