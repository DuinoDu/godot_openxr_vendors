.PHONY: build

build:
	./gradlew buildPlugin -PusePicoOpenxrLoader=true
	@rm -rf samples/pico-securemr-mnist/addons/godotopenxrvendors
	cp -r demo/addons/godotopenxrvendors samples/pico-securemr-mnist/addons/

rebuild:
	@rm -rf plugin/build
	make build

build-app:
	cd samples/pico-securemr-mnist; make build
install-app:
	cd samples/pico-securemr-mnist; make install
run-app:
	cd samples/pico-securemr-mnist; make run
stop-app:
	cd samples/pico-securemr-mnist; make stop
crash:
	adb logcat -d > crash.log

