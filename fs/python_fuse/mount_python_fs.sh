#!/bin/bash

MOUNT_POINT="/mnt/pythonfs"
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PYTHON_FS_SCRIPT="${SCRIPT_DIR}/python_fuse_fs.py"

# Check if python3 is available
if ! command -v python3 &> /dev/null
then
    echo "python3 could not be found, please install it."
    exit 1
fi

# Check if fusepy is installed (basic check, assumes pip list works)
# A more robust check might involve trying to import fuse in python
if ! python3 -m pip list | grep -q fusepy
then
    echo "fusepy is not installed. Please install it using: pip install -r requirements.txt"
    # Attempt to install if requirements.txt is present
    if [ -f "${SCRIPT_DIR}/../requirements.txt" ]; then
        echo "Attempting to install fusepy from requirements.txt..."
        python3 -m pip install -r "${SCRIPT_DIR}/../requirements.txt"
        if ! python3 -m pip list | grep -q fusepy
        then
            echo "Failed to install fusepy. Please install it manually."
            exit 1
        fi
    else
        exit 1
    fi
fi


# Create mount point if it doesn't exist
if [ ! -d "$MOUNT_POINT" ]; then
    echo "Creating mount point: $MOUNT_POINT"
    sudo mkdir -p "$MOUNT_POINT"
    sudo chown "$(whoami)":"$(id -gn)" "$MOUNT_POINT"
fi

# Check if already mounted
if mount | grep -q "on ${MOUNT_POINT} type fuse.python_fuse_fs"; then
    echo "Filesystem already mounted at ${MOUNT_POINT}."
    echo "To unmount, run: sudo umount ${MOUNT_POINT}"
    exit 0
fi

echo "Mounting Python FUSE filesystem at $MOUNT_POINT..."
# The script needs to be executable
chmod +x "$PYTHON_FS_SCRIPT"

# Run the Python FUSE script
# Using exec so that signals (like Ctrl+C) are passed to the FUSE process
exec python3 "$PYTHON_FS_SCRIPT" "$MOUNT_POINT"

# The script will typically be terminated by Ctrl+C, at which point FUSE handles unmounting.
# If not, unmount manually: sudo umount /mnt/pythonfs
# Or use fusermount -u /mnt/pythonfs (as non-root if user_allow_other is enabled in fuse.conf and allow_other in mount)
# For this basic script, we assume foreground execution and manual/Ctrl+C unmount.
