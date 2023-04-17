BUILD_DIR := build
DEBUG_DIR := debug
all:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake ..
	make -j -C $(BUILD_DIR)

debug:
	mkdir -p $(DEBUG_DIR)
	cd $(DEBUG_DIR) && cmake -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_BUILD_TYPE=Debug ..
	make VERBOSE=1 TSJ=1 -j -C $(DEBUG_DIR)

clean:
	rm -rf $(BUILD_DIR) ${DEBUG_DIR}

.PHONY: all clean