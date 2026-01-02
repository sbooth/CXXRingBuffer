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
		// Products define the executables and libraries a package produces, making them visible to other packages.
		.library(
			name: "CXXRingBuffer",
			targets: [
				"CXXRingBuffer",
			]
		),
	],
	targets: [
		// Targets are the basic building blocks of a package, defining a module or a test suite.
		// Targets can depend on other targets in this package and products from dependencies.
		.target(
			name: "CXXRingBuffer",
			cSettings: [
				.headerSearchPath("include/CXXRingBuffer"),
			]
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
