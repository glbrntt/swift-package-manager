/*
 This source file is part of the Swift.org open source project

 Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
 Licensed under Apache License v2.0 with Runtime Library Exception

 See http://swift.org/LICENSE.txt for license information
 See http://swift.org/CONTRIBUTORS.txt for Swift project authors
 */

import struct TSCUtility.Version
import struct SourceControl.Revision

/// A checkout state represents the current state of a repository.
///
/// A state will always has a revision. It can also have a branch or a version but not both.
public enum CheckoutState: Equatable, Hashable {
    case revision(_ revision: Revision)
    case version(_ version: Version, revision: Revision)
    case branch(name: String, revision: Revision)
}

extension CheckoutState: CustomStringConvertible {
    public var description: String {
        switch self {
        case .revision(let revision):
            return revision.identifier
        case .version(let version, _):
            return version.description
        case .branch(let branch, _):
            return branch
        }
    }
}
