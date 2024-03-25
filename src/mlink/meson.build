# Syntalos IPC Module Link Interface

sy_mlink_pub_hdr = [
    'modulelink.h',
]

sy_mlink_priv_hdr = [
    'ipc-types-private.h',
]

sy_mlink_src = [
    'modulelink.cpp',
]

sy_mlink_moc = qt.preprocess(
    moc_headers: [sy_mlink_pub_hdr],
    dependencies: [qt_core_dep],
)

syntalos_mlink_lib = library('syntalos-mlink',
    [sy_mlink_src,
     sy_mlink_pub_hdr,
     sy_mlink_priv_hdr,
     sy_mlink_moc],
    soversion: sy_mlink_abi_level,
    version: sy_version,
    dependencies: [
        qt_core_dep,
        iceoryx_posh_dep,
        iceoryx_hoofs_dep,
        syntalos_fabric_dep,
    ],
    include_directories: [],
    install: true
)

syntalos_mlink_dep = declare_dependency(
    link_with: syntalos_mlink_lib,
    sources: [
        sy_mlink_pub_hdr,
    ],
    dependencies: [qt_core_dep],
    include_directories: []
)

install_headers(sy_mlink_pub_hdr, subdir: 'syntalos' / 'mlink')

pkgc.generate(
    syntalos_mlink_lib,
    version: sy_version,
    name: 'syntalos-mlink',
    description: 'Library to build external Syntalos modules in separate processes.',
    filebase: 'syntalos-mlink',
    subdirs: 'syntalos/mlink',
    requires: ['Qt5Core']
)




test_client_exe = executable('mlink-test-client',
    ['test-client.cpp'],
    gnu_symbol_visibility: 'hidden',
    dependencies: [syntalos_mlink_dep,
                   syntalos_fabric_dep,
                ],
    include_directories: [root_include_dir],
    install: true,
)


test_master_hdr = [
    '../mlinkmodule.h',
]
test_master_src = [
    'test-master.cpp',
    '../mlinkmodule.cpp',
]

test_master_moc = qt.preprocess(
    moc_headers: [test_master_hdr],
    dependencies: [qt_core_dep],
)

test_master_exe = executable('mlink-test-master',
    [test_master_src,
     test_master_moc
    ],
    gnu_symbol_visibility: 'hidden',
    dependencies: [
        qt_core_dep,
        iceoryx_posh_dep,
        iceoryx_hoofs_dep,
        syntalos_fabric_dep,
    ],
    include_directories: [root_include_dir],
    install: true,
)
