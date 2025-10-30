.PHONY: build

build:
	./gradlew buildPlugin
	@rm -rf samples/pico-securemr-mnist/addons/godotopenxrvendors
	cp -r demo/addons/godotopenxrvendors samples/pico-securemr-mnist/addons/
