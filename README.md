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
Files with Signature "PA19"  are handled by falling back to mspatcha.dll.

| Pos | Content
|-|-|
| 0 – 3  | "PA30" |
| 4 – 11 | `TargetFileTime` (64bit LE int) <br> (100ns-Units since 1601-01-01) |
| 12 – ... | Bitstream w/ initial pad `0b000` (??), `FileTypeSet` (aka. `version`), `FileType` (aka. `code`), `Flags`, `TargetSize`, `TargetHashAlgId`, `TargetHash` ("Buffer", w/ int size + byte-aligned(??) content)

## BitStream
 * Bits are read/written LSB-first (!).
 * The stream seems to start with three 0-bits (?).
 * integral Numbers (up to 64 bit; 32bit: unsigned / 64bit: signed) are coded as follows:
    1. The number of nibbles (4-bit) required to represent the value (i.e. `nibbles = floor(log_2(value) / 4)`,
       with special `value == 0` treated like `1`) is written as `nibble + 1` bits with value `(1 << nibbles)`.
    2. The following `(nibbles + 1) * 4` bits are copied from  the value.
 * Buffers are written as integer size + padding to byte boundary (?) + content bytes.

Example:
Bit number |0|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|...
|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-
Start |0|0|0|
Len(14): 1 nibble ||||1|
Value: 14 |||||0|1|1|1|
Len(17): 2 nibbles |||||||||0|1|
Value: 17 |||||||||||1|0|0|0|1|0|0|0|

The result as hex dump:
```
e8 46 ...
```

## Contents
After the Header, the bitstream encodes two Buffers:
1. `preProcessBuffer`, containing information required for preprocessing depending of file type. The preprocessing also outputs a "rift table", which is corrected(?) for the size of the (to be prepended) source buffer.
2. `patchBuffer`, which consists of an (independent) Bitstream containing:
   1. a "base" rift table, which, merged with the preprocessing rift table, is obtained from / passed to the compressor, and
   2. compression parameters/statistics/tables(?) (in "composite format")
   3. the compressor bitstream

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
* After the Header the active bitstream coding is continued.
* "Normalization" / "Transform" of certain file types (esp. executables, processor architecture specific!), Rift Tables...
* But many PA30-files don't use/set those transforms/flags and are not "delta-a-previous-file" (source is empty buffer)!
* Compression? (`PseudoLzxCompress`, ... ?)
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
