// swift-tools-version: 5.9
//
// SPDX-FileCopyrightText: 2025 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXRingBuffer
//

import PackageDescription

let package = Package(
    name: "CXXRingBuffer",
    products: [
        .library(
            name: "CXXRingBuffer",
            targets: [
                "CXXRingBuffer",
            ]
        ),
    ],
    targets: [
        .target(
            name: "CXXRingBuffer"
        ),
        .testTarget(
            name: "CXXRingBufferTests",
            dependencies: [
                "CXXRingBuffer",
            ],
            swiftSettings: [
                .interoperabilityMode(.Cxx),
            ]
        ),
    ],
    cxxLanguageStandard: .cxx20
)
