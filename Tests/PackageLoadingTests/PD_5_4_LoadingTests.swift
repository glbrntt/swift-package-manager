/*
 This source file is part of the Swift.org open source project

 Copyright (c) 2020 - 2021 Apple Inc. and the Swift project authors
 Licensed under Apache License v2.0 with Runtime Library Exception

 See http://swift.org/LICENSE.txt for license information
 See http://swift.org/CONTRIBUTORS.txt for Swift project authors
*/

import Basics
import PackageLoading
import PackageModel
import SPMTestSupport
import TSCBasic
import XCTest

class PackageDescription5_4LoadingTests: PackageDescriptionLoadingTests {
    override var toolsVersion: ToolsVersion {
        .v5_4
    }

    func testExecutableTargets() throws {
        let content = """
            import PackageDescription
            let package = Package(
               name: "Foo",
               targets: [
                   .executableTarget(
                       name: "Foo"
                    ),
               ]
            )
            """

        let observability = ObservabilitySystem.makeForTesting()
        let manifest = try loadManifest(content, observabilityScope: observability.topScope)
        XCTAssertNoDiagnostics(observability.diagnostics)

        XCTAssertEqual(manifest.targets[0].type, .executable)
    }

    func testPluginsAreUnavailable() throws {
        let content = """
            import PackageDescription
            let package = Package(
               name: "Foo",
               targets: [
                   .plugin(
                       name: "Foo",
                       capability: .buildTool()
                   ),
               ]
            )
            """

        let observability = ObservabilitySystem.makeForTesting()
        XCTAssertThrowsError(try loadManifest(content, observabilityScope: observability.topScope), "expected error") { error in
            if case ManifestParseError.invalidManifestFormat(let message, _) = error {
                XCTAssertMatch(message, .contains("is unavailable"))
                XCTAssertMatch(message, .contains("was introduced in PackageDescription 5.5"))
            } else {
                XCTFail("unexpected error: \(error)")
            }
        }
    }
}
