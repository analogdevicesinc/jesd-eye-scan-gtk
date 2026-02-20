#!/bin/bash
set -e

version=$1
architecture=$(dpkg --print-architecture)
source_code=$(basename "$PWD")

# Use sudo only if not running as root
if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
else
    SUDO="sudo"
fi

$SUDO apt-get update
$SUDO apt-get install -y build-essential cmake pkg-config libgtk-3-dev libncurses-dev devscripts debhelper libiio-dev

# Update version and architecture in debian files
sed -i "s/@VERSION@/$version-1/" packaging/debian/changelog
sed -i "s/@DATE@/$(date -R)/" packaging/debian/changelog
sed -i "s/@ARCHITECTURE@/$architecture/" packaging/debian/control

# Copy debian directory to source root
cp -r packaging/debian .

# Remove packaging directory to avoid including it in the source tarball
rm -rf packaging

# Create the orig tarball
pushd ..
tar czf ${source_code}_${version}.orig.tar.gz \
    --exclude='.git' \
    --exclude='debian' \
    $source_code
popd

# Build the debian package
debuild
