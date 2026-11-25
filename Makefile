SCOOP ?= scoop
CLANG_FORMAT ?= clang-format
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
BINARY = target/$(TARGET)/release/dev.scoop/solar-system/solar-system
NATIVE_FLAGS = -Wall -Wextra -Werror -Inative/include -Ivendor/cspice/include

.PHONY: assets build run bundle format test test-native

assets:
	python3 scripts/prepare.py kernels
	python3 scripts/textures.py

build:
	python3 scripts/embed_shader.py
	$(SCOOP) build . --release --target $(TARGET) --library-path vendor/cspice/lib

run: build
	$(BINARY)

bundle: build
	$(SCOOP) bundle --executable $(BINARY) --config $(BUNDLE_CONFIG) --target $(TARGET) --output-dir dist

format:
	$(SCOOP) fmt .
	$(CLANG_FORMAT) -i native/astronomy/*.c native/include/*.h native/macos/*.m native/macos/*.h native/linux/*.c native/linux/*.h tests-native/*.c tests-native/*.h tests-native/*.m
	uv tool run --from ruff==0.16.10 ruff format scripts
	uv tool run --from ruff==0.16.10 ruff check scripts

test:
	$(SCOOP) test . --release --library-path vendor/cspice/lib

test-native:
	mkdir -p target
	$(NATIVE_CC) -std=c11 $(NATIVE_FLAGS) native/astronomy/ephemeris.c tests-native/ephemeris.c vendor/cspice/lib/cspice.a $(NATIVE_LIBS) -o target/check-ephemeris
	target/check-ephemeris assets/prepared
	python3 scripts/check_reference.py
ifeq ($(HOST_OS),Linux)
	$(NATIVE_CC) -std=c11 $(NATIVE_FLAGS) $(shell pkg-config --cflags gdk-pixbuf-2.0) tests-native/gles.c tests-native/gles_shadows.c native/astronomy/ephemeris.c native/linux/renderer.c native/linux/scene.c native/linux/shaders.c vendor/cspice/lib/cspice.a $(shell pkg-config --libs gdk-pixbuf-2.0 egl glesv2) -lm -o target/check-gles
	target/check-gles assets/prepared
else
	xcrun clang -fobjc-arc $(NATIVE_FLAGS) tests-native/metal.m tests-native/shadows.m native/astronomy/ephemeris.c native/macos/renderer.m native/macos/scene.m native/macos/shaders.m vendor/cspice/lib/cspice.a -framework Foundation -framework Metal -framework MetalKit -o target/check-metal
	target/check-metal assets/prepared
endif
