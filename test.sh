#!/bin/bash
set -eEuo pipefail

ok=true

for DEBUG                      in false true ; do
for USE_MEMCMP                 in false true ; do
for THREAD                     in THREAD_{BOOST,WINAPI} ; do
for SYNC                       in SYNC_{STD,BOOST,WINAPI,WINAPI_SPIN,INTEL_SPIN} ; do
for TLS                        in TLS_{COMPILER,WINAPI,BOOST} ; do
for DISK                       in DISK_{WINFILES,POSIX} ; do
for USE_UNBUFFERED_DISK_IO     in false true ; do
for MULTITHREADING             in false true ; do
for PREALLOCATE_COMBINING      in false true ; do
for DEBUG_EXPANSION            in false true ; do
# for ENABLE_EXPANSION_SPILLOVER in false true ; do
for USE_ALL                    in false true ; do

	echo "=============================================================="

	line=$(
		printf -- 'DEBUG=%-5s ' "$DEBUG"
		printf -- 'USE_MEMCMP=%-5s ' "$USE_MEMCMP"
		printf -- '%-20s ' "$THREAD"
		printf -- '%-20s ' "$SYNC"
		printf -- '%-20s ' "$TLS"
		printf -- '%-20s ' "$DISK"
		printf -- 'USE_UNBUFFERED_DISK_IO=%-5s ' "$USE_UNBUFFERED_DISK_IO"
		printf -- 'MULTITHREADING=%-5s ' "$MULTITHREADING"
		printf -- 'PREALLOCATE_COMBINING=%-5s ' "$PREALLOCATE_COMBINING"
		printf -- 'DEBUG_EXPANSION=%-5s ' "$DEBUG_EXPANSION"
		# printf -- 'ENABLE_EXPANSION_SPILLOVER=%-5s ' "$ENABLE_EXPANSION_SPILLOVER"
		printf -- 'USE_ALL=%-5s ' "$USE_ALL"
	)
	echo "$line"

	if [[ "$line" == *_WIN* && "$OS" != windows-* ]] ; then
		echo "Skipping (OS incompatibility)"
		echo "$line: Skipped" >> report.txt
		continue
	fi
	if [[ "$line" == *_POSIX* && "$OS" == windows-* ]] ; then
		echo "Skipping (OS incompatibility)"
		echo "$line: Skipped" >> report.txt
		continue
	fi
	if [[ "$SYNC" == SYNC_INTEL_SPIN ]] ; then
		# sync_intel_spin.cpp(18): error C4235: nonstandard extension used: '__asm' keyword not supported on this architecture
		# https://github.com/CyberShadow/DDD/issues/4
		echo "Skipping (TODO)"
		echo "$line: TODO!" >> report.txt
		continue
	fi
	if [[ "$TLS" == TLS_BOOST ]] ; then
		# tls_boost.cpp(1): fatal error C1189: #error:  TODO // See: http://www.boost.org/doc/libs/1_52_0/doc/html/thread/thread_local_storage.html
		echo "Skipping (TODO)"
		echo "$line: TODO!" >> report.txt
		continue
	fi
	if [[ "$TLS" == TLS_WINAPI ]] ; then
		# tls_winapi.cpp(1): fatal error C1189: #error:  TODO // See: http://msdn.microsoft.com/en-us/library/windows/desktop/ms686991(v=vs.85).aspx
		echo "Skipping (TODO)"
		echo "$line: TODO!" >> report.txt
		continue
	fi
	if [[ "$line" == *_BOOST* && "$OS" == windows-* ]] ; then
		# https://github.com/CyberShadow/DDD/issues/3
		echo "Skipping (TODO)"
		echo "$line: TODO!" >> report.txt
		continue
	fi
	if ! $MULTITHREADING ; then
		# search.cpp(1934): error C2065: 'WORKERS': undeclared identifier
		# https://github.com/CyberShadow/DDD/issues/1
		echo "Skipping (TODO)"
		echo "$line: TODO!" >> report.txt
		continue
	fi
	if $PREALLOCATE_COMBINING ; then
		# search.cpp(1023): error C2039: 'preallocate': is not a member of 'OutputStream<NODE>'
		# https://github.com/CyberShadow/DDD/issues/5
		echo "Skipping (TODO)"
		echo "$line: TODO!" >> report.txt
		continue
	fi
	if $DEBUG_EXPANSION ; then
		# search.cpp(1998): error C2065: 'TLS_TLS_GET_THREAD_ID': undeclared identifier
		# search.cpp(2074): error C2660: 'dumpExpansionDebug': function does not take 1 arguments
		# https://github.com/CyberShadow/DDD/issues/2
		echo "Skipping (TODO)"
		echo "$line: TODO!" >> report.txt
		continue
	fi

	args=(grep -v
		-e '#define DEBUG\b'
		-e '#define USE_MEMCMP\b'
		-e '#define THREAD_\(BOOST\|WINAPI\)\b'
		-e '#define SYNC_\(STD\|BOOST\|WINAPI\|WINAPI_SPIN\|INTEL_SPIN\)\b'
		-e '#define TLS_\(COMPILER\|WINAPI\|BOOST\)\b'
		-e '#define DISK_\(WINFILES\|POSIX\)\b'
		-e '#define USE_UNBUFFERED_DISK_IO\b'
		-e '#define MULTITHREADING\b'
		-e '#define PREALLOCATE_COMBINING\b'
		-e '#define DEBUG_EXPANSION\b'
		# -e '#define ENABLE_EXPANSION_SPILLOVER\b'
		-e '#define USE_ALL\b'
	)
	(
		"${args[@]}" < config-sample.h

		if $DEBUG                      ; then echo "#define DEBUG"                      ; fi
		if $USE_MEMCMP                 ; then echo "#define USE_MEMCMP"                 ; fi
		echo "#define $THREAD"
		echo "#define $SYNC"
		echo "#define $TLS"
		echo "#define $DISK"
		if $USE_UNBUFFERED_DISK_IO     ; then echo "#define USE_UNBUFFERED_DISK_IO"     ; fi
		if $MULTITHREADING             ; then echo "#define MULTITHREADING"             ; fi
		if $PREALLOCATE_COMBINING      ; then echo "#define PREALLOCATE_COMBINING"      ; fi
		if $DEBUG_EXPANSION            ; then echo "#define DEBUG_EXPANSION"            ; fi
		# if $ENABLE_EXPANSION_SPILLOVER ; then echo "#define ENABLE_EXPANSION_SPILLOVER" ; fi
		if $USE_ALL                    ; then echo "#define USE_ALL"                    ; fi
	) > config.h

	args=(
		"$COMPILER"
	)
	if [[ "$COMPILER" == cl ]] ; then
		args+=(
			-nologo
		)
	else
		args+=(
			-o search
		)
	fi
	if [[ "$line" == *_BOOST* ]] ; then
		args+=(
			-I"$BOOST_ROOT"/include
		)
	fi

	if ! "${args[@]}" search.cpp ; then
		echo "Compilation failed!"
		ok=false
		echo "$line: Compilation failed" >> report.txt
		continue
	fi

	find . -name '*.bin' -delete
	if ! ./search search ; then
		echo "Execution failed!"
		echo "$line: Execution failed" >> report.txt
		ok=false
		continue
	fi

	echo OK
	echo "$line: OK" >> report.txt
done
# done
done
done
done
done
done
done
done
done
done
done

echo "=============================================================="
echo "Summary:"
cat report.txt

if $ok ; then
	echo "All configurations passed!"
else
	echo "Some configurations failed!"
	exit 1
fi
