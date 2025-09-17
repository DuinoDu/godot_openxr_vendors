.PHONY: build clean

build:
	./gradlew -Ppico_headers=thirdparty/openxr_pico/include/openxr buildPlugin

clean:
	./gradlew clean
