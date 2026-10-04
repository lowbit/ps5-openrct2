# Runs inside the build image (docker/Dockerfile); see DEVELOPMENT.md.
NATIVE_TOOL := .deps/native-app/build/host/ps5-native-tool
SDK := .deps/native-app/.deps/native/ps5-payload-sdk

.PHONY: package ps5 deps runtime host launcher

package: ps5 launcher
	bash tools/package.sh

ps5: deps OpenRCT2/CMakeLists.txt
	bash tools/build-ps5.sh

deps: runtime
	bash tools/build-deps.sh

runtime: $(NATIVE_TOOL)
	bash tools/build-runtime.sh

host: OpenRCT2/CMakeLists.txt
	bash tools/build-host.sh

launcher: build/launcher/rct2launch.elf

build/launcher/rct2launch.elf: tools/launcher/launch.c $(NATIVE_TOOL)
	@mkdir -p $(dir $@)
	$(SDK)/bin/prospero-clang -O2 -Wall -o $@ $< -lSceSystemService -lSceUserService

$(NATIVE_TOOL):
	bash tools/fetch-native-tools.sh

OpenRCT2/CMakeLists.txt:
	bash tools/fetch-openrct2.sh
