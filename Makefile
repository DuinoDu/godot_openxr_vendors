.PHONY: build

build:
	./gradlew buildPlugin
	@rm -rf samples/pico-securemr-mnist/addons/godotopenxrvendors
	cp -r demo/addons/godotopenxrvendors samples/pico-securemr-mnist/addons/

log:
	adb logcat -c | adb logcat | grep -e 'XR_PICO_secure_mixed_reality|OpenXRPicoSecureMRExtensionWrapper'
