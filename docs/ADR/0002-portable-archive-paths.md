# 0002: Portable ASCII game-member paths in schema v1

Game installation roots and locale labels support Unicode. Signed *relative
game member paths* are restricted to printable ASCII without Windows forbidden
characters, reserved DOS names, trailing dots/spaces, or ambiguous components.
They are compared with ASCII case folding on both operating systems. Component
prefixes are checked too, preventing `Data/a` and `data/b` directory aliases.

This is a conservative deviation from accepting arbitrary Unicode archive
member names: safe cross-platform Unicode normalization/case folding requires
a maintained Unicode database, which the chosen small dependency stack does
not supply. Rejecting unsupported names is safer than treating them as unique.
The publisher fails with a specific path error; it never renames a game member
silently. A future schema can add a well-defined normalization implementation.

Limits: envelope 16 MiB, payload 12 MiB, depth 48, one million JSON values,
100,000 items per array, paths 1,024 bytes / 32 components, each component 255
bytes, package 1.5 GiB, expanded package 64 GiB, base target 512 GiB. Individual
game files must fit in a package. The publisher reserves ZIP overhead below the
package cap. These bounds fail closed, without allocating package contents.

Developer fixture executables are synthetic files during M2. They are never
executed; a real native fixture program is required for game-launch tests.
