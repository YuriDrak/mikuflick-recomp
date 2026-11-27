.PHONY: deps sim device clean distclean
deps:
	@sh App/fetch-deps.sh

sim: SDK = iphonesimulator
device: SDK = iphoneos
CONFIG ?= Debug
SIGNING ?= NO
# optional, for signed device builds: TEAM=<team id> BUNDLE_ID=<your bundle id>
TEAM ?=
BUNDLE_ID ?=

sim device: deps
	cmake -S . -B build/$(SDK) -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
	    -DCMAKE_OSX_SYSROOT=$(SDK) -DCMAKE_OSX_ARCHITECTURES=arm64 \
	    -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
	    -DCMAKE_XCODE_GENERATE_SCHEME=ON -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=$(SIGNING) \
	    $(if $(TEAM),-DMF_TEAM_ID=$(TEAM)) $(if $(BUNDLE_ID),-DMF_BUNDLE_ID=$(BUNDLE_ID))
	cmake --build build/$(SDK) --config $(CONFIG) --target MikuFlick \
	    $(if $(filter YES,$(SIGNING)),-- -allowProvisioningUpdates)

# generated projects and build products; keeps the verified SDL sources in build/deps
clean:
	rm -rf build/iphoneos build/iphonesimulator

distclean:
	rm -rf build
