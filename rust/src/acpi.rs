// This module maps the data returned from call into the C-Library to RUST structures
unsafe extern "C" {
    fn EvaluateAcpi(input: *const i8, input_len: usize, buffer: *mut u8, buf_len: &mut usize) -> i32;
}

// A user-friendly ACPI input method containing a name and optional arguments
struct AcpiMethodInput<'a, 'b> {
    name: &'a str,
    args: Option<&'b [AcpiMethodArgument]>,
}

/// A user-friendly ACPI method argument
#[derive(Debug, Copy, Clone)]
pub enum AcpiMethodArgument {
    /// Arbitrary u32 integer (DWORD)
    Int(u32),
    /// Arbitrary string
    Str(&'static str),
    /// GUID in mixed-endian format
    Guid(uuid::Bytes),
}

// Convert a user-friendly ACPI method argument to format expected by driver
impl TryFrom<AcpiMethodArgument> for AcpiMethodArgumentV1 {
    type Error = AcpiParseError;
    fn try_from(arg: AcpiMethodArgument) -> Result<Self, AcpiParseError> {
        Ok(match arg {
            AcpiMethodArgument::Guid(g) => Self {
                type_: 2,
                data_length: 16,
                data_32: 0,
                data: g.to_vec(),
            },
            AcpiMethodArgument::Str(s) => {
                let cstr = std::ffi::CString::new(s).map_err(|_| AcpiParseError::InvalidFormat)?;
                Self {
                    type_: 1,
                    data_length: cstr.count_bytes() as u16 + 1,
                    data_32: 0,
                    data: cstr.as_bytes_with_nul().to_vec(),
                }
            }
            AcpiMethodArgument::Int(i) => Self {
                type_: 0,
                data_length: 4,
                data_32: i,
                data: i.to_le_bytes().to_vec(),
            },
        })
    }
}

#[repr(C)]
#[derive(Debug)]
pub struct AcpiEvalInputBufferComplexV1Ex {
    pub signature: u32,
    pub methodname: [u8; 256],
    pub size: u32,
    pub argumentcount: u32,
    pub arguments: Vec<AcpiMethodArgumentV1>,
}

#[repr(C)]
#[derive(Debug, Default)]
pub struct AcpiEvalOutputBufferV1 {
    pub signature: u32,
    pub length: u32,
    pub count: u32,
    pub arguments: Vec<AcpiMethodArgumentV1>,
}

#[repr(C)]
#[derive(Debug, Default)]
pub struct AcpiMethodArgumentV1 {
    pub type_: u16,
    pub data_length: u16,
    pub data_32: u32,
    pub data: Vec<u8>,
}

#[derive(Debug)]
pub enum AcpiParseError {
    InsufficientLength,
    InvalidFormat,
}

pub const ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE_EX: u32 = u32::from_le_bytes(*b"AeiF");

impl std::fmt::Display for AcpiParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}

// Convert a user-friendly ACPI input method to format expected by driver
impl TryFrom<AcpiMethodInput<'_, '_>> for AcpiEvalInputBufferComplexV1Ex {
    type Error = AcpiParseError;
    fn try_from(method: AcpiMethodInput) -> Result<Self, AcpiParseError> {
        let mut buffer = [0u8; 256];
        let bytes = method.name.as_bytes();
        let len = bytes.len().min(256);
        buffer[..len].copy_from_slice(&bytes[..len]);

        let arguments = if let Some(args) = method.args {
            args.iter()
                .map(|&arg| AcpiMethodArgumentV1::try_from(arg))
                .collect::<Result<Vec<_>, _>>()?
        } else {
            Vec::default()
        };
        let size = arguments.iter().map(|arg| arg.data_length as u32).sum();

        Ok(AcpiEvalInputBufferComplexV1Ex {
            signature: ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE_EX,
            methodname: buffer,
            size,
            argumentcount: arguments.len() as u32,
            arguments,
        })
    }
}

// Convert ACPI input struct to a raw, packed byte buffer
impl From<AcpiEvalInputBufferComplexV1Ex> for Vec<u8> {
    fn from(input: AcpiEvalInputBufferComplexV1Ex) -> Self {
        let mut buf = Vec::new();
        buf.extend(&input.signature.to_le_bytes());
        buf.extend(&input.methodname);
        buf.extend(&input.size.to_le_bytes());
        buf.extend(&input.argumentcount.to_le_bytes());

        for arg in input.arguments.iter() {
            buf.extend(&arg.type_.to_le_bytes());
            buf.extend(&arg.data_length.to_le_bytes());
            buf.extend(&arg.data);
        }

        buf
    }
}

// Convert vec[u8] into AcpiEvalOutputBufferV1
impl TryFrom<Vec<u8>> for AcpiEvalOutputBufferV1 {
    type Error = AcpiParseError;
    fn try_from(value: Vec<u8>) -> Result<Self, AcpiParseError> {
        let signature = u32::from_le_bytes(value[0..4].try_into().map_err(|_| AcpiParseError::InvalidFormat)?);
        let length = u32::from_le_bytes(value[4..8].try_into().map_err(|_| AcpiParseError::InvalidFormat)?);
        let count = u32::from_le_bytes(value[8..12].try_into().map_err(|_| AcpiParseError::InvalidFormat)?);

        let mut offset = 12;
        let mut arguments = Vec::new();

        for _ in 0..count {
            if offset + 8 > value.len() {
                return Err(AcpiParseError::InsufficientLength);
            }

            let type_ = u16::from_le_bytes(value[offset..offset + 2].try_into().unwrap());
            let data_length = u16::from_le_bytes(value[offset + 2..offset + 4].try_into().unwrap()) as usize;
            let data_32 = if type_ == 0 {
                u32::from_le_bytes(value[offset + 4..offset + 8].try_into().unwrap())
            } else {
                0
            };
            offset += 4;

            if offset + data_length > value.len() {
                return Err(AcpiParseError::InsufficientLength);
            }

            let data = value[offset..offset + data_length].to_vec();
            offset += data_length;

            arguments.push(AcpiMethodArgumentV1 {
                type_,
                data_length: data_length as u16,
                data_32,
                data,
            });
        }

        // Now return generated content
        Ok(AcpiEvalOutputBufferV1 {
            signature,
            length,
            count,
            arguments,
        })
    }
}

pub struct Acpi {}

impl Acpi {
    pub fn evaluate(name: &str, args: Option<&[AcpiMethodArgument]>) -> Result<AcpiEvalOutputBufferV1, AcpiParseError> {
        // Maximum number of arguments allowed is 7 as per spec
        if let Some(args) = args
            && args.len() > 7
        {
            return Err(AcpiParseError::InsufficientLength);
        }

        let method = AcpiMethodInput { name, args };
        let input = AcpiEvalInputBufferComplexV1Ex::try_from(method)?;

        // Input buffer
        let in_buf: Vec<u8> = input.into();
        let in_buf_len = in_buf.len();

        // Output buffer
        let mut out_buf_len = 1024;
        let mut out_buf = vec![0u8; out_buf_len];

        let _res = unsafe {
            EvaluateAcpi(
                in_buf.as_ptr() as *const i8,
                in_buf_len,
                out_buf.as_mut_ptr(),
                &mut out_buf_len,
            )
        };

        AcpiEvalOutputBufferV1::try_from(out_buf)
    }
}
