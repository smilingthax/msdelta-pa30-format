# MSDELTA / PA30 patch file format

A "PA30" patch file encodes the differences between a "source" buffer/file and a "target" buffer/files;  
a patch can be applied (`ApplyDelta(A|W|B)`) to a source file to obtain the target file. The patch file also includes the expected hash value of the resulting target file. This also prevents using a "wrong" source file.  
Patch files can be created via `CreateDelta(A|W|B)` / `DeltaFree`, resp. `ApplyDeltaProvidedB`.  
Further methods in `msdelta.dll` are: `GetDeltaInfo(A|W|B)`, `DeltaNormalizeProvidedB`, and `GetDeltaSignature(A|W|B)`.

Patch files "without a source" are also commonly used (e.g. IPD updates in CAB files, accessible via `expand -F:*`) by passing an empty buffer as source.

## Header

Signature + `DELTA_HEADER_INFO` from `GetDeltaInfo`, e.g.:
```
DELTA_HEADER_INFO:
  FileTypeSet: 0x000000000000000f
  FileType: 0x0000000000000001
  Flags: 0x0000000000020000
  TargetSize: 12345
  TargetFileTime: 0x01cd456789abcdef (unix: 1339153925)
  TargetHashAlgId: 0x00008003  (i.e. CALG_MD5)
  TargetHash:
    HashSize: 16
    HashValue: 00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff
```
Files with Signature "PA19" are handled by falling back to mspatcha.dll (when DELTA_APPLY_FLAG_ALLOW_PA19 is given).

| Pos | Content
|-|-|
| 0 – 3  | "PA30" |
| 4 – 11 | `TargetFileTime` (64bit LE int: 100ns-Units since 1601-01-01) |
| 12 – ... | Outer bitstream

## BitStreams

 * Bits are read/written LSB-first.
 * The stream starts with three bits that code for the number of padding bits in the very last byte.
 * integral Numbers (up to 64 bit; 32bit: unsigned / 64bit: signed) are coded as follows:
    1. The number of nibbles (4-bit) required to represent the value (i.e. `nibbles = floor(log_2(value) / 4)`,
       with special `value == 0` treated like `1`) is written as `nibble + 1` bits with value `(1 << nibbles)`.
    2. The following `(nibbles + 1) * 4` bits are copied from  the value.
 * Buffers are written as integer number size + padding to byte boundary + content bytes.

Example:
Bit number |0|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|...
|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-
Pad: 1 |1|0|0|
Len(14): 1 nibble ||||1|
Value: 14 |||||0|1|1|1|
Len(17): 2 nibbles |||||||||0|1|
Value: 17 |||||||||||1|0|0|0|1|0|0|0|

The result as hex dump:
```
e9 46 ...
```

## Outer bitstream

| Type | Content |
|-|-|
| **-- Header --** |
| number (64bit?) | `FileTypeSet` (aka. `version`) |
| number (64bit?) | `FileType` (aka. `code`) |
| number (64bit?) | `Flags` |
| number | `TargetSize` |
| number | `TargetHashAlgId` |
| buffer | `TargetHash` |
| **-- Contents --** |
| buffer | `preProcessBuffer` |
| buffer | `patchBuffer` |

## Contents

After the Header, the bitstream encodes two more Buffers:
1. `preProcessBuffer`, which consists of an (independent) Bitstream containing information required for preprocessing, dependent on file type, possibly empty (length = 0).
   The bitstream also contains, resp. the preprocessing also outputs, a "rift table",
   which is corrected(?) for the size of the (to be prepended) source buffer.
2. `patchBuffer`, another bitstream containing:
   1. a "base" rift table, which is merged with the preprocessing rift table prior to decompression,
   2. compression parameters (in "composite format"), and
   3. the compressor bitstream

Canonical huffman codes are used, thus only the bit-lengths used to code each symbol have to be transmitted to reconstruct the code at the receiver. Only "complete" codes are used (?).

Given a desired number of symbols, `size`, "default lengths" for a complete huffman are defined, where each symbol is given the same weight/frequency. With  `len = floor(log_2(size - 1)) + 1` each symbol therefore has length `len - 1` or `len`, with the shorter length used for lower-indexed symbols to avoid an incomplete code when `size` is not a power of 2.

LZX Delta (https://interoperability.blob.core.windows.net/files/MS-PATCH/%5bMS-PATCH%5d.pdf) describes the specifics (enumeration order, ...) of the canonical huffmann code (Note: DEFLATE uses right-leaning tree, but here a left-leaning tree is used!).

## Patch buffer

| Type | Content |
|-|-|
| Rift table <br> (see below) | Base rift table, starting with single bit `isNonEmpty`. |
| **-- Compression Parameters --** | ("Composite Format") |
| 1 bit | `isDefault`: A single block with "default" huffman lengths. The bitstream directly continues with the actual Content in this case. |
| number | `num_blocks`: Number of parameter blocks used. |
| number[num_blocks] | Delta-encoded (init = 0) start offsets, i.e. where the corresponding block parameters take effect.
| (4 bits)[39] | Huffman pre-table used to encode the lenghts of the actual huffman tables.
| (872 lengths)[num_blocks] | RLE delta + Huffman coded (see below) lengths (wrt. previous block, init = all-0)
| **-- Content --** | |
| ... | Huffman coded matches, using parameters for the block corresponding to the current output position. |

The 872 lengths  (= 0x368)  parameters of each block are actually the lengths of three separate huffman trees, `main tree` (0x258 symbols), `lengths tree` (0x100 symbols), `aligned offset tree` (0x10 symbols) concatenated in that order; all three trees use max. 16 bit long codes.  
For `isDefault` each of the three trees uses the "default lengths" (see above) for their respective size.

### RLE delta coding of lengths
| Symbol number | Meaning |
|-|-|
| 0 – 16 | Verbatim, copy number to output |
| 17 / 18 / 19 | Length from previous block +1 / +2 / +3 |
| 20 / 21 / 22 | Length from previous block -1 / -2 / -3 |
| 23 – 30 | RLE fill with last written length (not possibly for very first length of each block) |
| 31 – 38 | RLE copy lengths from previous block |

The length for the RLE operation is:
1. For the first 3 symbol numbers (23/24/25 resp. 31/32/33) the length is 1/2/3,
2. otherwise 2 – 6 more (raw) bits are read from the bitstream, which are combined with an implicit leading 1-bit.

Pseudo-code:
```
  lencode = (sym - 23) & 7
  if (lencode < 3)
     len = lencode + 1
  else
     len = (1 << (lencode - 1)) | READ_BITS(lencode - 1)
```

### Matches
There are different types of matches, coded with the `main tree`:
1. A literal byte (symbol = 0 ... 255, implicit `length = 1`),
2. copy `(offset, length)` from `output_position - offset`,
3. copy `(delta, length)` from `output_position + rift_offset(output_position) + delta` (TODO: signs?), must not cross a rift-boundary,
4. copy `(length)` from `output_position + rift_offset(output_position)` (TODO??) across multiple rift segments, or
5. copy `(lru_index, length)` from `output_position + lru[lru_index]` with a three-element least-recently-used queue ("repeat match"), updated after every non-literal match (NOTE: unlike LZX Delta, a full implementation is used here).

For non-literal matches, the symbol number encodes a pair `(slot, length) = ((sym - 256) >> 3, (sym - 256) & 7)`.  Depending on the slot number, additional bits of the offset then follow.

For `length == 0` the final match length is then coded with the `length tree`, with possibly additional bits coded in a var-length format.

TODO: describe slot codings  
TODO: describe length var-length-format

## Rift table

List of intervals `[left, right)`, stored as pairs of integers with their own huffman coding... (? - TODO...)

| Type | Content |
|-|-|
| 1 bit | `isNonEmpty`: if `0`, the following fields are all skipped. |
| ... | TODO. |

### Huffman encoding
TODO

## Preprocessing buffer

This depends on the file type, with the focus on PE-Executables. The actually used transforms are stored in the `Flags` field.  
The preprocessing results in an altered "source" buffer and a "rift table". ... (TODO) ...

## Related Patents (expired)

* "Method and system for updating software with smaller patch files" https://patents.google.com/patent/US6938109B1/en  
  General idea of pre-filling the compression dictionary / window ("prepend the old date")
  with contents from an "old file" (i.e. already present at both source and destination),
  which can be (deterministically) preprocessed/transformed (e.g. reordered, irrelevant sections removed, jump addresses rewritten [e.g.  `RiftTransformRelativeJmpsI386`)], ...).
* "Preprocessing a reference data stream for patch generation and compression"  https://patents.google.com/patent/US6466999/en  
  Describes the normalization / preprocessing steps, "rift table" for block movement / reordering, ...
* "Temporally ordered binary search method and system"  https://patents.google.com/patent/US5978795A/en  
  Technique to efficiently implement LZ77 / LZX compression.
* "Inter-delta dependent containers for content delivery" https://patents.google.com/patent/US20070260653  
   -> Intra Package Delta format in CAB archives,    `_manifest_.cix.xml` ...

## TODO

* "Normalization" / "Transform" of certain file types (esp. executables, processor architecture specific!), Rift Tables...
* But many PA30-files don't use/set those transforms/flags and are not "delta-a-previous-file" (source is empty buffer)!
* Post-Processing (file-type specific...)

### Processing graph strings (implementation detail?)
Using `strings` on `msdelta.dll` reveals that Microsoft®'s patch engine implementation builds different "processing graphs" at runtime from string representations, e.g.:
```
blockname1( CopyBuffer ): input[ 0 ];
asciiSignature( TToAscii ): "PA30";
asciiSignatureSize( BufferSize ): signature[ 0 ];
signature( Unconcat ): blockname1[ 0 ], asciiSignatureSize[ 0 ];
checkSignature( CheckBuffersIdentity ): signature[ 0 ], asciiSignature[ 0 ], 0x041234;
blockname5( BitReaderOpen ): signature[ 1 ];
[...] output: [...]
```
* The exchanged data can be (at least) Integers and Buffers.
* There are special "block names" `input`, `output` and `wrap`.
* Blocks can have multiple inputs (after `:`, separated by `,`) and outputs (accessed later via `blockname[ ? ]`)
* An opened `BitReader` (or is it a Buffer?) is "chained" through multiple `BitReadInt` via `[ 0 ]`, with the obtained int value in `[ 1 ]`...
* `TToAscii` seems to be implemented as "subgraph" `wrap( CopyBuffer ):;`
* Graphs seem to come only from hardcoded strings, thus the whole processing graph w/ dynamic setup stuff is probably not needed in a compatible implementation (?; multi-threading?).
Still, the string representations helps to get an understanding of the format.
* Some interesting Blocks:  `PassIniReader`, `DebugWriteFile`
* The third argument to `CheckBuffersIdentity` is probably an internal error code thrown when the buffers don't match...
* The string representation has more features, e.g. `( Blocktype / flags[ 0 ] ? "BlocktypeABC" : "Blocktype" )`...
