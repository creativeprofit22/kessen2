# SPDX-License-Identifier: GPL-3.0-or-later
#
# Module-boundary enforcement for the Kessen II port.
#
#   k2_register_guarded(<target>...)
#       Mark external targets (e.g. ps2_runtime, SDL3::SDL3) as "guarded": a k2
#       module may only depend on them if they are in its ALLOWS list.
#
#   k2_add_module(<target> [ALLOWS <target>...])
#       Register an existing target as a k2 module and record which guarded
#       targets / other modules it may depend on. Every module is guarded too.
#
#   k2_verify_module_graph()
#       Call once, after every target_link_libraries(). Direct edges are
#       deny-by-default: every target in a module's LINK_LIBRARIES /
#       INTERFACE_LINK_LIBRARIES must be in its ALLOWS list, guarded or not
#       (non-target items such as plain library names or flags are ignored).
#       Allowlisted unguarded helpers are then walked transitively and any edge
#       to a guarded target that is not allowlisted fails configuration too.
#       Also fails if ${K2_GENERATED_DIR} appears in the include directories of
#       a module that is not allowed to use k2_generated.
#
# See docs/ARCHITECTURE.md and docs/adr/0004-module-boundaries-and-composition-root.md.

include_guard(GLOBAL)

define_property(TARGET PROPERTY K2_ALLOWED_DEPS
    BRIEF_DOCS "Guarded targets this k2 module may depend on"
    FULL_DOCS "Set by k2_add_module(); checked by k2_verify_module_graph().")

function(_k2_resolve_alias out_var name)
    set(resolved "${name}")
    if(TARGET "${name}")
        get_target_property(aliased "${name}" ALIASED_TARGET)
        if(aliased)
            set(resolved "${aliased}")
        endif()
    endif()
    set(${out_var} "${resolved}" PARENT_SCOPE)
endfunction()

function(k2_register_guarded)
    foreach(t IN LISTS ARGN)
        if(NOT TARGET "${t}")
            message(FATAL_ERROR "k2_register_guarded: '${t}' is not a target")
        endif()
        _k2_resolve_alias(real "${t}")
        set_property(GLOBAL APPEND PROPERTY K2_GUARDED_TARGETS "${real}")
    endforeach()
endfunction()

function(k2_add_module target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "ALLOWS")
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "k2_add_module(${target}): unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "k2_add_module: '${target}' is not a target (create it first)")
    endif()
    set(allowed "")
    foreach(a IN LISTS ARG_ALLOWS)
        # Allowlist entries may name targets that are optional in this
        # configuration (e.g. k2_generated before any code is generated).
        _k2_resolve_alias(real "${a}")
        list(APPEND allowed "${real}")
    endforeach()
    set_property(TARGET "${target}" PROPERTY K2_ALLOWED_DEPS "${allowed}")
    set_property(GLOBAL APPEND PROPERTY K2_MODULES "${target}")
    set_property(GLOBAL APPEND PROPERTY K2_GUARDED_TARGETS "${target}")
endfunction()

# Extract every target named in one link item (handles $<LINK_ONLY:...>,
# other generator expressions and ::@(dir) scope markers conservatively).
function(_k2_link_item_targets out_var item)
    set(found "")
    if(item MATCHES "\\$<" OR item MATCHES "::@")
        string(REGEX MATCHALL "[A-Za-z0-9_.+-]+(::[A-Za-z0-9_.+-]+)*" tokens "${item}")
    else()
        set(tokens "${item}")
    endif()
    foreach(tok IN LISTS tokens)
        if(TARGET "${tok}")
            _k2_resolve_alias(real "${tok}")
            list(APPEND found "${real}")
        endif()
    endforeach()
    set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

function(_k2_direct_deps out_var target)
    set(deps "")
    foreach(prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(items "${target}" ${prop})
        if(NOT items)
            continue()
        endif()
        foreach(item IN LISTS items)
            _k2_link_item_targets(ts "${item}")
            list(APPEND deps ${ts})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES deps)
    set(${out_var} "${deps}" PARENT_SCOPE)
endfunction()

function(k2_verify_module_graph)
    get_property(modules GLOBAL PROPERTY K2_MODULES)
    get_property(guarded GLOBAL PROPERTY K2_GUARDED_TARGETS)
    set(errors "")

    foreach(mod IN LISTS modules)
        get_target_property(allowed "${mod}" K2_ALLOWED_DEPS)
        if(NOT allowed)
            set(allowed "")
        endif()

        # Direct edges are deny-by-default: every target the module links must be
        # allowlisted, guarded or not (third-party helpers included).
        _k2_direct_deps(direct "${mod}")
        set(queue "")
        foreach(dep IN LISTS direct)
            if(NOT dep IN_LIST allowed)
                string(APPEND errors "\n  ${mod} -> ${dep}   (allowed: ${allowed})")
            elseif(NOT dep IN_LIST guarded)
                # Allowlisted unguarded helper: walk it for smuggled guarded edges.
                list(APPEND queue "${dep}")
            endif()
        endforeach()

        # Transitive walk through allowlisted helpers: report guarded targets,
        # descend through unguarded ones.
        set(visited "${mod}" ${direct})
        set(pending "")
        foreach(h IN LISTS queue)
            _k2_direct_deps(more "${h}")
            list(APPEND pending ${more})
        endforeach()
        set(queue "${pending}")
        while(queue)
            list(POP_FRONT queue dep)
            if(dep IN_LIST visited)
                continue()
            endif()
            list(APPEND visited "${dep}")
            if(dep IN_LIST guarded)
                if(NOT dep IN_LIST allowed)
                    string(APPEND errors "\n  ${mod} -> ${dep}   (allowed: ${allowed})")
                endif()
                # Guarded targets are verified on their own; don't descend.
                continue()
            endif()
            _k2_direct_deps(more "${dep}")
            list(APPEND queue ${more})
        endwhile()

        # Generated-code include rule.
        if(DEFINED K2_GENERATED_DIR AND NOT "${mod}" STREQUAL "k2_generated"
           AND NOT "k2_generated" IN_LIST allowed)
            file(TO_CMAKE_PATH "${K2_GENERATED_DIR}" gen_dir)
            string(TOLOWER "${gen_dir}" gen_dir_lc)
            foreach(prop IN ITEMS INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
                get_target_property(dirs "${mod}" ${prop})
                if(NOT dirs)
                    continue()
                endif()
                foreach(d IN LISTS dirs)
                    file(TO_CMAKE_PATH "${d}" d_norm)
                    string(TOLOWER "${d_norm}" d_lc)
                    string(FIND "${d_lc}" "${gen_dir_lc}" pos)
                    if(NOT pos EQUAL -1)
                        string(APPEND errors "\n  ${mod} includes generated dir via ${prop}: ${d}")
                    endif()
                endforeach()
            endforeach()
        endif()
    endforeach()

    if(errors)
        message(FATAL_ERROR
            "K2 module dependency violation(s):${errors}\n"
            "See docs/ARCHITECTURE.md (dependency rule). Only the app target may compose everything.")
    endif()
    list(LENGTH modules n)
    message(STATUS "K2 module graph OK (${n} modules checked)")
endfunction()
