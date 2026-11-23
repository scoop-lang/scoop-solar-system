SCOOP ?= scoop
TARGET ?= aarch64-apple-darwin
CLANG_FORMAT ?= clang-format
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
	$(SCOOP) bundle --executable $(BINARY) --config packaging/macos.toml --target $(TARGET) --output-dir dist

format:
	$(SCOOP) fmt .
	$(CLANG_FORMAT) -i native/astronomy/*.c native/include/*.h native/macos/*.m native/macos/*.h tests-native/*.c tests-native/*.m
	uv tool run --from ruff==0.16.10 ruff format scripts
	uv tool run --from ruff==0.16.10 ruff check scripts

test:
	$(SCOOP) test . --release --library-path vendor/cspice/lib

test-native:
	mkdir -p target
	xcrun clang -std=c11 $(NATIVE_FLAGS) native/astronomy/ephemeris.c tests-native/ephemeris.c vendor/cspice/lib/cspice.a -o target/check-ephemeris
	target/check-ephemeris assets/prepared
	python3 scripts/check_reference.py
	xcrun clang -fobjc-arc $(NATIVE_FLAGS) tests-native/metal.m tests-native/shadows.m native/astronomy/ephemeris.c native/macos/renderer.m native/macos/scene.m native/macos/shaders.m vendor/cspice/lib/cspice.a -framework Foundation -framework Metal -framework MetalKit -o target/check-metal
	target/check-metal assets/prepared
