#!/usr/bin/env bash

set -euo pipefail

module_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
nginx_source="$module_root/.ci/nginx"
test_output="$module_root/.ci/test-output"
build_jobs="${TEST_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
mkdir -p "$test_output"
exec > >(tee "$test_output/run.log") 2>&1

configure_args=(
    --prefix="$nginx_source/test-prefix"
    --with-debug
    --with-http_ssl_module
    --with-http_v2_module
    --with-http_realip_module
    --with-http_sub_module
    --with-http_json_module
    --with-stream
    --with-stream_ssl_module
    --with-stream_realip_module
)

configure_args+=(--add-module="$module_root")
configure_args+=(--add-module="$module_root/t/ngx_http_expr_time_test_module")

cd "$nginx_source"
configure=./configure
[[ -f "$configure" ]] || configure=auto/configure
sh "$configure" "${configure_args[@]}"
make -j"$build_jobs"

core_extensions=0
if grep -q '^#define NGX_RESTY_EXT  *1$' objs/ngx_auto_config.h; then
    core_extensions=1
fi

export TEST_NGINX_BINARY="$nginx_source/objs/nginx"
export PERL5LIB="$module_root/.ci/nginx-tests/lib"
export TMPDIR="$(mktemp -d /tmp/nginx-module-test.XXXXXX)"
export TEST_NGINX_LEAVE=1
export TEST_NGINX_SERVROOT="$TMPDIR/servroot"
collect_logs() {
    find "$TMPDIR" -type f -name '*.log' -exec cp --parents {} "$test_output/" \;
}
trap collect_logs EXIT
if [[ "$(id -u)" == 0 ]]; then
    export TEST_NGINX_GLOBALS="${TEST_NGINX_GLOBALS:-user root;}"
fi
"$TEST_NGINX_BINARY" -V

make -f objs/Makefile -f "$module_root/t/unit/Makefile" expr-when-id-test \
    EXPR_MODULE="$module_root" EXPR_TEST_BINARY="$test_output/when-id"

cd "$module_root"
shopt -s nullglob
tests=()
for test_file in t/*.t; do
    if [[ "$core_extensions" == 0 ]]; then
        case "$test_file" in
            t/expr_core.t|t/expr_invalid.t|t/expr_complex_time.t) ;;
            *) echo "SKIP: $test_file (requires conditional core directives)"
               continue ;;
        esac
    fi
    tests+=("$test_file")
done
if (( ${#tests[@]} == 0 )); then
    echo "No tests selected" >&2
    exit 1
fi
prove -v "${tests[@]}" | tee "$test_output/tests.log"
grep -q '^Result: PASS$' "$test_output/tests.log"
