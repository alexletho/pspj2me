# Convenience entry points.
#   make host   - build the host (Linux) test runner and the class library (build/boot.jar)
#   make psp    - build EBOOT.PBP (requires pspdev toolchain in PATH)
#   make test   - run VM + MIDlet tests on the host
#   make dist   - assemble dist/PSP/GAME/PSPJ2ME for the memory stick
.PHONY: host psp test dist clean
host:
	$(MAKE) -f Makefile.host
psp: host
	$(MAKE) -f Makefile.psp
test: host
	sh tests/run_tests.sh
dist: host
	$(MAKE) -f Makefile.psp clean-sfo
	$(MAKE) -f Makefile.psp
	python3 tools/package.py

clean:
	$(MAKE) -f Makefile.host clean
	rm -rf build dist EBOOT.PBP PARAM.SFO *.elf *.prx
