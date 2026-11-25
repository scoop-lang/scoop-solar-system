CLANG_FORMAT ?= clang-format
NATIVE_FLAGS = -Wall -Wextra -Werror -Inative/include -Ivendor/cspice/include
ifeq ($(OS),Windows_NT)
# The Windows driver is sco.exe; the PATH name scoop is the package manager.
SCOOP ?= sco
TARGET ?= x86_64-pc-windows-msvc-ucrt
BUNDLE_CONFIG = packaging/windows.toml
EXE = .exe
# Native checks use the toolchain's clang-cl with the MSVC toolset and SDK
# found by vswhere, the same installation the Scoop driver links against.
SCOOP_SYSROOT ?= $(subst \,/,$(USERPROFILE))/.scoop/toolchains/$(word 2,$(shell $(SCOOP) --version))-x86_64-pc-windows-gnullvm
CLANG_CL ?= $(SCOOP_SYSROOT)/libexec/llvm/bin/clang-cl.exe
VSWHERE ?= C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe
VS_ROOT := $(subst \,/,$(shell "$(VSWHERE)" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath))
VC_VERSION := $(shell cat "$(VS_ROOT)/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt")
VC_REDIST_VERSION := $(shell cat "$(VS_ROOT)/VC/Auxiliary/Build/Microsoft.VCRedistVersion.default.txt")
WINDOWS_SDK ?= C:/Program Files (x86)/Windows Kits/10
# clang-cl reads -Wall as -Weverything; -Wmost matches the Cone flags.
MSVC_FLAGS = -nologo -O2 -EHsc -MD -D_CRT_SECURE_NO_WARNINGS -Wmost -Wextra -Werror -Inative/include -Ivendor/cspice/include -vctoolsdir "$(VS_ROOT)/VC/Tools/MSVC/$(VC_VERSION)" -winsdkdir "$(WINDOWS_SDK)"
# The installer deploys the MSVC C++ runtime next to the program.
VC_RUNTIME = target/vc-runtime
else
SCOOP ?= scoop
HOST_OS := $(shell uname -s)
ifeq ($(HOST_OS),Linux)
TARGET ?= x86_64-unknown-linux-gnu
BUNDLE_CONFIG = packaging/linux.toml
NATIVE_CC = clang
NATIVE_LIBS = -lm
else
TARGET ?= aarch64-apple-darwin
BUNDLE_CONFIG = packaging/macos.toml
NATIVE_CC = xcrun clang
endif
endif
BINARY = target/$(TARGET)/release/dev.scoop/solar-system/solar-system$(EXE)

.PHONY: assets build run bundle format test test-native

assets:
	python3 scripts/prepare.py kernels
	python3 scripts/textures.py

build:
	python3 scripts/embed_shader.py
	$(SCOOP) build . --release --target $(TARGET) --library-path vendor/cspice/lib

run: build
	$(BINARY)

ifeq ($(OS),Windows_NT)
bundle: build
	mkdir -p $(VC_RUNTIME)
	for dll in msvcp140 vcruntime140 vcruntime140_1; do cp "$(VS_ROOT)/VC/Redist/MSVC/$(VC_REDIST_VERSION)/x64/"Microsoft.VC*.CRT/$$dll.dll $(VC_RUNTIME)/; done
	$(SCOOP) bundle --executable $(BINARY) --config $(BUNDLE_CONFIG) --target $(TARGET) --output-dir dist
else
bundle: build
	$(SCOOP) bundle --executable $(BINARY) --config $(BUNDLE_CONFIG) --target $(TARGET) --output-dir dist
endif

format:
	$(SCOOP) fmt .
	$(CLANG_FORMAT) -i native/astronomy/*.c native/include/*.h native/macos/*.m native/macos/*.h native/linux/*.c native/linux/*.h native/windows/*.cpp native/windows/*.h tests-native/*.c tests-native/*.cpp tests-native/*.h tests-native/*.m
	uv tool run --from ruff==0.16.10 ruff format scripts
	uv tool run --from ruff==0.16.10 ruff check scripts

test:
	$(SCOOP) test . --release --library-path vendor/cspice/lib

ifeq ($(OS),Windows_NT)
test-native:
	mkdir -p target/check
	"$(CLANG_CL)" $(MSVC_FLAGS) -c native/astronomy/ephemeris.c -Fotarget/check/solar-ephemeris.obj
	"$(CLANG_CL)" $(MSVC_FLAGS) tests-native/ephemeris.c target/check/solar-ephemeris.obj vendor/cspice/lib/cspice.lib -Fetarget/check-ephemeris.exe -Fotarget/check/
	target/check-ephemeris.exe assets/prepared
	CLANG_CL="$(CLANG_CL)" MSVC_FLAGS='$(MSVC_FLAGS)' python3 scripts/check_reference.py
	"$(CLANG_CL)" $(MSVC_FLAGS) tests-native/d3d12.cpp tests-native/target_shadows.c target/check/solar-ephemeris.obj native/windows/renderer.cpp native/windows/scene.cpp native/windows/shaders.cpp native/windows/textures.cpp vendor/cspice/lib/cspice.lib d3d12.lib dxgi.lib d3dcompiler.lib windowscodecs.lib ole32.lib -Fetarget/check-d3d12.exe -Fotarget/check/
	target/check-d3d12.exe assets/prepared
else
test-native:
	mkdir -p target
	$(NATIVE_CC) -std=c11 $(NATIVE_FLAGS) native/astronomy/ephemeris.c tests-native/ephemeris.c vendor/cspice/lib/cspice.a $(NATIVE_LIBS) -o target/check-ephemeris
	target/check-ephemeris assets/prepared
	python3 scripts/check_reference.py
ifeq ($(HOST_OS),Linux)
	$(NATIVE_CC) -std=c11 $(NATIVE_FLAGS) $(shell pkg-config --cflags gdk-pixbuf-2.0) tests-native/gles.c tests-native/target_shadows.c native/astronomy/ephemeris.c native/linux/renderer.c native/linux/scene.c native/linux/shaders.c vendor/cspice/lib/cspice.a $(shell pkg-config --libs gdk-pixbuf-2.0 egl glesv2) -lm -o target/check-gles
	target/check-gles assets/prepared
else
	xcrun clang -fobjc-arc $(NATIVE_FLAGS) tests-native/metal.m tests-native/shadows.m native/astronomy/ephemeris.c native/macos/renderer.m native/macos/scene.m native/macos/shaders.m vendor/cspice/lib/cspice.a -framework Foundation -framework Metal -framework MetalKit -o target/check-metal
	target/check-metal assets/prepared
endif
endif
