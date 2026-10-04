# Runs inside the build image (docker/Dockerfile); see DEVELOPMENT.md.
NATIVE_TOOL := .deps/native-app/build/host/ps5-native-tool

.PHONY: ps5 deps runtime host

ps5: deps OpenRCT2/CMakeLists.txt
	bash tools/build-ps5.sh

deps: runtime
	bash tools/build-deps.sh

runtime: $(NATIVE_TOOL)
	bash tools/build-runtime.sh

host: OpenRCT2/CMakeLists.txt
	bash tools/build-host.sh

$(NATIVE_TOOL):
	bash tools/fetch-native-tools.sh

OpenRCT2/CMakeLists.txt:
	bash tools/fetch-openrct2.sh
