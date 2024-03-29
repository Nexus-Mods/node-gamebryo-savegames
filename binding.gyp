{
    "targets": [
        {
            "target_name": "GamebryoSave",
            "includes": [
                "auto.gypi"
            ],
            "cflags!": [ "-fno-exceptions" ],
            "cflags_cc!": [ "-fno-exceptions" ],
            "sources": [
                "src/gamebryosavegame.cpp",
                "src/fmt/format.cc"
            ],
            "include_dirs": [
                "<!(node -p \"require('node-addon-api').include_dir\")",
            ],
            "dependencies": [
              "<!(node -p \"require('node-addon-api').gyp\")"
            ],
            "conditions": [
                ['OS=="win"', {
                    "include_dirs": [
                        "./lz4/include",
                        "./zlib/include"
                    ],
                    "libraries": [
                        "-l../lz4/dll/liblz4",
                        "-l../zlib/lib/zlib",
                        "-DelayLoad:node.exe"
                    ],
                    "defines": [
                        "UNICODE",
                        "_UNICODE"
                    ],
                    "msvs_settings": {
                        "VCCLCompilerTool": {
                            "ExceptionHandling": 1
                        }
                    }
                }]
            ]
        }
    ],
    "includes": [
        "auto-top.gypi"
    ]
}
