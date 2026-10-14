#!/bin/bash
# Shared selection policy (case labels and guard script basenames).
QUICK_21='^(demo[1-5]-d4|scenario-(water|spotlight|flip|blur)-d4|modern-demo1-d4|modern-wide-demo1-d4|modern-crt-scanlines|depth-tyrian-on-d4|depth-held-pause-wide-d4|depth-held-pause-smooth-d4|modern-pause)$'
QUICK_2000='^(demo1-d4|modern-demo1-16x9-d4|depth-e1-level16-2p-on-d4|data-open-audit)$'
case_selected() {
    [[ "$1" =~ $CASE_FILTER ]] || return 1
    if [ "$QUICK" -eq 1 ]; then
        local quick_filter=$QUICK_21
        [ "${SUITE_VARIANT:-2.1}" = 2000 ] && quick_filter=$QUICK_2000
        [[ "$1" =~ $quick_filter ]] || return 1
    fi
    return 0
}
guard_selected() {
    # An explicit area filter selects guards by their script name.
    if [ "$CASE_FILTER" != '.*' ]; then [[ "$1" =~ $CASE_FILTER ]]; return; fi
    [ "$QUICK" -eq 0 ] && return 0
    case "$1" in
        check_no_t2000_data.sh|check_user_paths.sh|check_variant_bootstrap.sh|check_game_rules.sh|check_bike_effort.sh|check_bike.sh) return 0 ;;
    esac
    return 1
}

validate_case_filter() {
    local rc=0
    [[ '' =~ $CASE_FILTER ]]
    rc=$?
    if [ "$rc" -eq 2 ]; then echo "ERROR: invalid case regex: $CASE_FILTER" >&2; return 2; fi
}
