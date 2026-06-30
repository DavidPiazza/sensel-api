# Sensel API

The Sensel API allows users to communicate with Sensel devices. 

[Sensel API Primer](http://guide.sensel.com/api/)

[Sensel Lib Documentation](http://guide.sensel.com/sensel_h/)

[Sensel Decompress Lib Documentation](http://guide.sensel.com/sensel_decompress_h/)

## Apple Silicon Support

Sensel never shipped a prebuilt Apple Silicon (arm64) binary, but the core
library builds natively from source — it's portable MIT-licensed C that talks to
the Morph over a USB serial port, with no architecture-specific code.

To build a native (or universal arm64 + x86_64) `libSensel.dylib` and install it
where the wrappers expect it:

```sh
cd sensel-lib
./build_mac.sh
sudo cp build/mac/libSensel.dylib /usr/local/lib/libSensel.dylib   # /usr/local/lib may already be writable
```

That's all that's needed for full multitouch on Apple Silicon: each contact
reports position, **total force (in grams)**, area, ellipse, bounding box, and
peak — see `sensel-examples/sensel-python/example_2_sensel_contacts.py`.

**One caveat — the raw force image.** The full per-element force array
(`FRAME_CONTENT_PRESSURE_MASK`, used by `example_3`) is decompressed by Sensel's
closed-source `libSenselDecompress`, which is only distributed as an x86_64
binary. There is no arm64 build available, so to read the raw force image on
Apple Silicon you must run your app under Rosetta against x86_64 builds of both
libraries (`./build_mac.sh pressure`). Per-contact force does **not** need the
decompress library and works natively.

### Python 3

The Python examples and wrapper run on Python 3. The examples under
`sensel-examples/sensel-python/` have been ported from Python 2, and the wrapper
in `sensel-lib-wrappers/sensel-lib-python/sensel.py` works as-is on Python 3.

## Getting Started

Before getting started with this API, make sure your device's firmware is up to date. Go to [Sensel's Start Page](http://sensel.com/start) to get the latest version of the SenselApp and update your device.

To run the examples, first install the Sensel libraries found in sensel-install. After installing the libraries, you can open one of the Sensel source examples (C, Python and C&#35;). 

The source for the central Sensel library can be found in sensel-lib. This includes all of the necessary functions to communicate with Sensel devices except for force frame decompression. More information can be found below.

## sensel-install

The sensel-install directory contains installers for Windows, Mac and Linux that places Sensel libraries and headers into the appropriate directory for each operating system. 

There are two libraries installed on each operating system. LibSensel contains the core functionality for communicating with Sensel devices. LibSenselDecompress allows you to read and decompress force frames from Sensel devices. We provide the source for LibSensel in the sensel-lib directory, which is covered under the MIT License, and can be compiled to not link or depend on LibSenselDecompress.  

### Windows: 

SenselLibWin.exe installs the libraries and headers into C:\\Program Files\\Sensel\\Sense Lib\\ with the following directories: include for the headers, x86 for 32-bit compiled .dll and .lib, and x64 for 64-bit compiled .dll and .lib. Each test should recognize whether the process being run is 32-bit or 64-bit and reference the appropriate libraries. 

### Mac: 

SenselLibMac.pkg installs the libraries into /usr/local/lib and the headers into /usr/local/include. The installer only includes a 64-bit version of the libraries. If a 32-bit version is required, then see the sensel-lib section below to see how to compile the core library. 

### Linux

senselliblinux.deb installs the libraries into /usr/lib and the headers into /usr/include. The installer only includes a 64-bit version of the libraries. If a 32-bit version is required, then see the sensel-lib section below to see how to compile the core library. 

## sensel-examples

The sensel-examples directory includes examples for C, C&#35;, and Python to demonstrate how to use the Sensel libraries for each language. 

### Sensel Examples

#### example-1-hello-sensel

Example 1 demonstrates how to find Sensel devices, open the device, and request information about from the device. 

#### example-2-contacts

Example 2 demonstrates how to connect to a Sensel Device, start scanning for contacts, and read a frame of data for contacts.

#### example-3-forces

Example 3 demonstrates how to connect to a Sensel Device, start scanning for forces, and read a frame of force data. In this example, all the forces are being summed to report the total force.

#### example-4-multi

Example 4 demonstrates how to connect to multiple Sensel Devices, start scanning for contacts, and read a frame of data for contacts.

## sensel-lib

The sensel-lib directory contains the source for the core Sensel library. LibSensel is covered under the MIT license. LibSensel contains the main functionality for communicating with Sensel devices, but does not include force frame decompression, which is contained in LibSenselDecompress. Both libraries are included in the installers found in sensel-install, but LibSensel can be recompiled to be a standalone library (no dependencies on LibSenselDecompress) or to replace the existing LibSensel and support force frames.

### Build without Forces (Standalone)

The sensel-lib directory builds without forces by default. This can be done in Visual Studio for Windows, xCode for Mac and the Makefile for Linux. 

If you want to test this new library then you can either install the library into the appropriate install directory for each operating system or build the tests to reference a library in the same directory as the test. 

### Build with Forces 

To build the LibSensel with force, simply define SENSEL_PRESSURE. This will build LibSensel with references to LibSenselDecompress. 

To install this library, replace the existing library and headers, making sure to leave LibSenselDecompress and sensel_decompress.h to ensure proper force frame functionality. 
