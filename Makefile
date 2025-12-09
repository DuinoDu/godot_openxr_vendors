.PHONY: build

clean:
	./gradlew clean

build:
	./gradlew buildPlugin -PusePicoOpenxrLoader=true
	@rm -rf samples/pico-securemr-mnist/addons/godotopenxrvendors
	cp -r demo/addons/godotopenxrvendors samples/pico-securemr-mnist/addons/

rebuild:
	@rm -rf plugin/build
	make build

proj=/home/duino/ws/godot/object_tracking

install-proj:
	@rm -rf $(proj)/addons/godotopenxrvendors
	cp -r demo/addons/godotopenxrvendors $(proj)/addons/
