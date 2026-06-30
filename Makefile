CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer -DHAVE_LIBTLS
LDFLAGS = -fsanitize=address,undefined

# TLS: libretls (provides libtls API over system OpenSSL)
PKG_CFLAGS  := $(shell pkg-config --cflags libtls)
PKG_LDFLAGS := $(shell pkg-config --libs libtls)

INC = -Iinclude -Itests

SRC_DIR = src
TEST_DIR = tests

SRC_OBJ = $(SRC_DIR)/cb_json.o $(SRC_DIR)/cb_http.o $(SRC_DIR)/cb_config.o \
          $(SRC_DIR)/cb_validate.o $(SRC_DIR)/cb_api.o $(SRC_DIR)/cb_cli.o

TEST_BINS = $(TEST_DIR)/test_json $(TEST_DIR)/test_http $(TEST_DIR)/test_config \
            $(TEST_DIR)/test_validate $(TEST_DIR)/test_api $(TEST_DIR)/test_cli

.PHONY: all test clean install

all: cb

cb: $(SRC_OBJ) $(SRC_DIR)/main.o
	$(CC) $(LDFLAGS) -o $@ $(SRC_OBJ) $(SRC_DIR)/main.o $(PKG_LDFLAGS) -lm

# Compile src objects
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) $(INC) -c -o $@ $<

# Compile test objects
$(TEST_DIR)/%.o: $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) $(INC) -I$(TEST_DIR) -c -o $@ $<

# Test binaries: each links against needed src objects + pthread (for mock server)
$(TEST_DIR)/test_json: $(TEST_DIR)/test_json.o $(SRC_DIR)/cb_json.o
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LDFLAGS) -lm

$(TEST_DIR)/test_http: $(TEST_DIR)/test_http.o $(TEST_DIR)/mock_server.o $(SRC_DIR)/cb_http.o $(SRC_DIR)/cb_json.o
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LDFLAGS) -lpthread -lm

$(TEST_DIR)/test_config: $(TEST_DIR)/test_config.o $(SRC_DIR)/cb_config.o
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LDFLAGS)

$(TEST_DIR)/test_validate: $(TEST_DIR)/test_validate.o $(SRC_DIR)/cb_validate.o
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LDFLAGS)

$(TEST_DIR)/test_api: $(TEST_DIR)/test_api.o $(TEST_DIR)/mock_server.o $(SRC_DIR)/cb_api.o $(SRC_DIR)/cb_http.o $(SRC_DIR)/cb_json.o $(SRC_DIR)/cb_validate.o $(SRC_DIR)/cb_config.o
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LDFLAGS) -lpthread -lm

$(TEST_DIR)/test_cli: $(TEST_DIR)/test_cli.o $(TEST_DIR)/mock_server.o $(SRC_DIR)/cb_cli.o $(SRC_DIR)/cb_api.o $(SRC_DIR)/cb_http.o $(SRC_DIR)/cb_json.o $(SRC_DIR)/cb_validate.o $(SRC_DIR)/cb_config.o
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LDFLAGS) -lpthread -lm

test: $(TEST_BINS)
	@for bin in $(TEST_BINS); do \
		echo ""; \
		echo "=== $$bin ==="; \
		$$bin || exit 1; \
	done
	@echo ""; echo "All tests passed."

clean:
	rm -f cb $(SRC_OBJ) $(SRC_DIR)/main.o $(TEST_DIR)/*.o $(TEST_BINS)

PREFIX ?= $(HOME)/.local

install: cb
	install -d $(PREFIX)/bin
	install -m 755 cb $(PREFIX)/bin/cb

.PHONY: format
format:
	@if command -v clang-format >/dev/null 2>&1; then \
	    echo "Formatting C with clang-format..."; \
	    find src include tests \
	        \( -name '*.c' -o -name '*.h' \) -print0 \
	        | xargs -0 clang-format -i; \
	else \
	    echo "clang-format not found - skipping C formatting." >&2; \
	fi
	@if command -v prettier >/dev/null 2>&1; then \
	    echo "Formatting Markdown with prettier..."; \
	    find . -maxdepth 2 -name '*.md' -print0 \
	        | xargs -0 prettier --write; \
	else \
	    echo "prettier not found - skipping Markdown formatting." >&2; \
	fi
