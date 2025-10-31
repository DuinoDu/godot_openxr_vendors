.PHONY: build

build:
	./gradlew buildPlugin -PusePicoOpenxrLoader=true
	@rm -rf samples/pico-securemr-mnist/addons/godotopenxrvendors
	cp -r demo/addons/godotopenxrvendors samples/pico-securemr-mnist/addons/

rebuild:
	@rm -rf plugin/build
	make build
