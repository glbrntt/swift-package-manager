/*
 This source file is part of the Swift.org open source project

 Copyright (c) 2014 - 2021 Apple Inc. and the Swift project authors
 Licensed under Apache License v2.0 with Runtime Library Exception

 See http://swift.org/LICENSE.txt for license information
 See http://swift.org/CONTRIBUTORS.txt for Swift project authors
*/

import Basics
import PackageModel
import TSCBasic

extension Basics.Diagnostic {
    static func targetHasNoSources(targetPath: String, target: String) -> Self {
        .warning("Source files for target \(target) should be located under \(targetPath)")
    }

    static func targetNameHasIncorrectCase(target: String) -> Self {
        .warning("the target name \(target) has different case on the filesystem and the Package.swift manifest file")
    }

    static func unsupportedCTestTarget(package: String, target: String) -> Self {
        .warning("ignoring target '\(target)' in package '\(package)'; C language in tests is not yet supported")
    }

    static func duplicateProduct(product: Product) -> Self {
        let typeString: String
        switch product.type {
        case .library(.automatic):
            typeString = ""
        case .executable, .snippet, .plugin, .test,
             .library(.dynamic), .library(.static):
            typeString = " (\(product.type))"
        }

        return .warning("ignoring duplicate product '\(product.name)'\(typeString)")
    }

    static func duplicateTargetDependency(dependency: String, target: String, package: String) -> Self {
        .warning("invalid duplicate target dependency declaration '\(dependency)' in target '\(target)' from package '\(package)'")
    }

    static var systemPackageDeprecation: Self {
        .warning("system packages are deprecated; use system library targets instead")
    }

    static func systemPackageDeclaresTargets(targets: [String]) -> Self {
        .warning("ignoring declared target(s) '\(targets.joined(separator: ", "))' in the system package")
    }

    static func systemPackageProductValidation(product: String) -> Self {
        .error("system library product \(product) shouldn't have a type and contain only one target")
    }

    static func executableProductTargetNotExecutable(product: String, target: String) -> Self {
        .error("""
            executable product '\(product)' expects target '\(target)' to be executable; an executable target requires \
            a 'main.swift' file
            """)
    }

    static func executableProductWithoutExecutableTarget(product: String) -> Self {
        .error("""
            executable product '\(product)' should have one executable target; an executable target requires a \
            'main.swift' file
            """)
    }

    static func executableProductWithMoreThanOneExecutableTarget(product: String) -> Self {
        .error("executable product '\(product)' should not have more than one executable target")
    }

    static func pluginProductWithNoTargets(product: String) -> Self {
        .error("plugin product '\(product)' should have at least one plugin target")
    }

    static func pluginProductWithNonPluginTargets(product: String, otherTargets: [String]) -> Self {
        .error("plugin product '\(product)' should have only plugin targets (it has \(otherTargets.map{ "'\($0)'" }.joined(separator: ", ")))")
    }

    static var noLibraryTargetsForREPL: Self {
        .error("unable to synthesize a REPL product as there are no library targets in the package")
    }

    static func brokenSymlink(_ path: AbsolutePath) -> Self {
        .warning("ignoring broken symlink \(path)")
    }

    static func conflictingResource(path: RelativePath, targetName: String) -> Self {
        .error("multiple resources named '\(path)' in target '\(targetName)'")
    }

    static func fileReference(path: RelativePath) -> Self {
        .info("found '\(path)'")
    }

    static func infoPlistResourceConflict(
        path: RelativePath,
        targetName: String
    ) -> Self {
        .error("""
            resource '\(path)' in target '\(targetName)' is forbidden; Info.plist is not supported as a top-level \
            resource file in the resources bundle
            """)
    }

    static func copyConflictWithLocalizationDirectory(path: RelativePath, targetName: String) -> Self {
        .error("resource '\(path)' in target '\(targetName)' conflicts with other localization directories")
    }

    static func missingDefaultLocalization() -> Self {
        .error("missing manifest property 'defaultLocalization'; it is required in the presence of localized resources")
    }

    static func localizationAmbiguity(path: RelativePath, targetName: String) -> Self {
        .error("""
            resource '\(path)' in target '\(targetName)' is in a localization directory and has an explicit \
            localization declaration in the package manifest; choose one or the other to avoid any ambiguity
            """)
    }

    static func localizedAndUnlocalizedVariants(resource: String, targetName: String) -> Self {
        .warning("""
            resource '\(resource)' in target '\(targetName)' has both localized and un-localized variants; the \
            localized variants will never be chosen
            """)
    }
}

extension ObservabilityMetadata {
    public var manifestLoadingDiagnosticFile: AbsolutePath? {
        get {
            self[ManifestLoadingDiagnosticFileKey.self]
        }
        set {
            self[ManifestLoadingDiagnosticFileKey.self] = newValue
        }
    }

    enum ManifestLoadingDiagnosticFileKey: Key {
        typealias Value = AbsolutePath
    }
}
