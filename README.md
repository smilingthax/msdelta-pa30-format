# MSDELTA / PA30 patch file format

## Header
Signature + `DELTA_HEADER_INFO` from `GetDeltaInfo`, e.g.:
```
DELTA_HEADER_INFO:
  FileTypeSet: 0x000000000000000f
  FileType: 0x0000000000000001
  Flags: 0x0000000000020000
  TargetSize: 12345
  TargetFileTime: 0x01cd456789abcdef (unix: 13391539251)
  TargetHashAlgId: 0x00008003  (i.e. CALG_MD5)
  TargetHash:
    Size: 16
    HashValue: 00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff
```
Files with Signature "PA19"  are handled by falling back to mspatcha.dll.

| Pos | Content
|-|-|
| 0 – 4  | "PA30" |
| 5 – 12 | `TargetFileTime` (64bit LE int) <br> (100ns-Units since 1601-01-01) |
| 13 – ... | Bitstream w/ initial pad `0b000` (??), `FileTypeSet` (aka. `version`), `FileType` (aka. `code`), `Flags`, `TargetSize`, `TargetHashAlgId`, `TargetHash` ("Buffer", w/ int size + byte-aligned(??) content)

 ## BitStream
 * Bits are read/written LSB-first (!).
 * The stream seems to start with three 0-bits (?).
 * (unsigned) integers (up to 64 bit) are coded as follows:
    1. The number of nibbles (4-bit) required to represent the value (i.e. `nibbles = floor(BitScanForward(value) / 4)`,  with special `value == 0` treated like `1`) is written as `nibble + 1` bits with value `(1 << nibbles)`.
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

## TODO
* After the Header the active bitstream coding is continued.
* Certain file types (esp. executables, processor architecture specific!) can be "transformed"(aka. "Normalized" ?) prior to being diffed / compressed (as described in http://www.google.com/patents/US20070260653), e.g. absolute jump addresses (which will change with every byte insertion) are stored as relative offsets (e.g.  `RiftTransformRelativeJmpsI386`).
* But many PA30-files don't use/set those transforms/flags and are not "delta-a-previous-file" (i.e. empty buffer is used as source)!
* Compression? (`PseudoLzxCompress`, ... ?)

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
* Graphs seem to come only from hardcoded strings, thus the whole processing graph w/ dynamic setup stuff is probably not needed in a compatible implementation (?).
Still, the string representations helps to get an understanding of the format.
* Some interesting Blocks:  `PassIniReader`, `DebugWriteFile`
* The third argument to `CheckBuffersIdentity` is probably an internal error code thrown when the buffers don't match...
* The string representation has more features, e.g. `( Blocktype / flags[ 0 ] ? "BlocktypeABC" : "Blocktype" )`...

