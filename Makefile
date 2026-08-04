#
# FRANK universal test firmware
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# https://github.com/rh1tech/frank-test
# SPDX-License-Identifier: GPL-3.0-or-later
#

.DEFAULT_GOAL := help

.PHONY: help hooks build flash clean

help: ## Show this help
	@grep -hE '^[a-z_-]+:.*?## ' $(MAKEFILE_LIST) | awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'

hooks: ## Point git at the repo's hooks (.githooks)
	@git config core.hooksPath .githooks
	@echo "core.hooksPath = .githooks"

build: ## Build the test firmware for the default board
	@cmake -S app -B app/build -DPICO_BOARD=$(or $(BOARD),frank_core2_master)
	@cmake --build app/build -j8

flash: ## Flash over USB (hold BOOTSEL first)
	@picotool load -x app/build/frank-test.uf2

clean: ## Remove build trees
	@rm -rf app/build app/build-*
