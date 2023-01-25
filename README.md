


# Dependencies:


`sudo apt install cmake build-essential autoconf libtool`

`sudo apt install liblimesuite-dev libjson-c-dev`

# Compile

`git submodule update --init`
`make`

## LimeSDR-mini udev rules (allow non-root use)

`sudo cp limesdr-mini.rules /etc/udev/rules.d/`
