use std::collections::HashMap;
use std::io::{self, Read, Write};

/// Tag types matching the original NBT specification.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum TagType {
    End = 0,
    Byte = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Float = 5,
    Double = 6,
    ByteArray = 7,
    String = 8,
    List = 9,
    Compound = 10,
}

impl TagType {
    pub fn from_u8(v: u8) -> Self {
        match v {
            0 => TagType::End,
            1 => TagType::Byte,
            2 => TagType::Short,
            3 => TagType::Int,
            4 => TagType::Long,
            5 => TagType::Float,
            6 => TagType::Double,
            7 => TagType::ByteArray,
            8 => TagType::String,
            9 => TagType::List,
            10 => TagType::Compound,
            _ => TagType::End,
        }
    }
}

/// A Named Binary Tag (NBT) structure.
#[derive(Debug, Clone, PartialEq)]
pub enum Tag {
    End,
    Byte(i8),
    Short(i16),
    Int(i32),
    Long(i64),
    Float(f32),
    Double(f64),
    ByteArray(Vec<u8>),
    String(String),
    List(TagType, Vec<Tag>),
    Compound(HashMap<String, Tag>),
}

impl Tag {
    pub fn id(&self) -> u8 {
        match self {
            Tag::End => TagType::End as u8,
            Tag::Byte(_) => TagType::Byte as u8,
            Tag::Short(_) => TagType::Short as u8,
            Tag::Int(_) => TagType::Int as u8,
            Tag::Long(_) => TagType::Long as u8,
            Tag::Float(_) => TagType::Float as u8,
            Tag::Double(_) => TagType::Double as u8,
            Tag::ByteArray(_) => TagType::ByteArray as u8,
            Tag::String(_) => TagType::String as u8,
            Tag::List(_, _) => TagType::List as u8,
            Tag::Compound(_) => TagType::Compound as u8,
        }
    }

    pub fn new_compound() -> Self {
        Tag::Compound(HashMap::new())
    }

    pub fn put_byte(&mut self, name: &str, value: i8) {
        if let Tag::Compound(map) = self {
            map.insert(name.to_string(), Tag::Byte(value));
        }
    }

    pub fn put_short(&mut self, name: &str, value: i16) {
        if let Tag::Compound(map) = self {
            map.insert(name.to_string(), Tag::Short(value));
        }
    }

    pub fn put_int(&mut self, name: &str, value: i32) {
        if let Tag::Compound(map) = self {
            map.insert(name.to_string(), Tag::Int(value));
        }
    }

    pub fn put_long(&mut self, name: &str, value: i64) {
        if let Tag::Compound(map) = self {
            map.insert(name.to_string(), Tag::Long(value));
        }
    }

    pub fn put_float(&mut self, name: &str, value: f32) {
        if let Tag::Compound(map) = self {
            map.insert(name.to_string(), Tag::Float(value));
        }
    }

    pub fn put_string(&mut self, name: &str, value: String) {
        if let Tag::Compound(map) = self {
            map.insert(name.to_string(), Tag::String(value));
        }
    }

    pub fn put_compound(&mut self, name: &str, value: Tag) {
        if let Tag::Compound(map) = self {
            if matches!(value, Tag::Compound(_)) {
                map.insert(name.to_string(), value);
            }
        }
    }

    pub fn get(&self, name: &str) -> Option<&Tag> {
        if let Tag::Compound(map) = self {
            map.get(name)
        } else {
            None
        }
    }

    pub fn as_i64(&self) -> i64 {
        match self {
            Tag::Long(v) => *v,
            Tag::Int(v) => *v as i64,
            Tag::Short(v) => *v as i64,
            Tag::Byte(v) => *v as i64,
            _ => 0,
        }
    }

    pub fn as_i32(&self) -> i32 {
        match self {
            Tag::Int(v) => *v,
            Tag::Short(v) => *v as i32,
            Tag::Byte(v) => *v as i32,
            _ => 0,
        }
    }

    pub fn as_str(&self) -> &str {
        match self {
            Tag::String(v) => v,
            _ => "",
        }
    }

    pub fn get_compound(&self, name: &str) -> Option<&HashMap<String, Tag>> {
        if let Some(Tag::Compound(map)) = self.get(name) {
            Some(map)
        } else {
            None
        }
    }

    pub fn write<W: Write>(&self, writer: &mut W) -> io::Result<()> {
        match self {
            Tag::End => Ok(()),
            Tag::Byte(v) => writer.write_all(&[*v as u8]),
            Tag::Short(v) => writer.write_all(&v.to_be_bytes()),
            Tag::Int(v) => writer.write_all(&v.to_be_bytes()),
            Tag::Long(v) => writer.write_all(&v.to_be_bytes()),
            Tag::Float(v) => writer.write_all(&v.to_be_bytes()),
            Tag::Double(v) => writer.write_all(&v.to_be_bytes()),
            Tag::ByteArray(v) => {
                writer.write_all(&(v.len() as i32).to_be_bytes())?;
                writer.write_all(v)
            }
            Tag::String(v) => {
                let bytes = v.as_bytes();
                writer.write_all(&(bytes.len() as u16).to_be_bytes())?;
                writer.write_all(bytes)
            }
            Tag::List(ty, elements) => {
                writer.write_all(&[*ty as u8])?;
                writer.write_all(&(elements.len() as i32).to_be_bytes())?;
                for element in elements {
                    element.write(writer)?;
                }
                Ok(())
            }
            Tag::Compound(map) => {
                for (name, tag) in map {
                    Self::write_named(tag, name, writer)?;
                }
                writer.write_all(&[TagType::End as u8])?;
                Ok(())
            }
        }
    }

    pub fn read<R: Read>(reader: &mut R, ty: TagType) -> io::Result<Tag> {
        match ty {
            TagType::End => Ok(Tag::End),
            TagType::Byte => {
                let mut buf = [0u8; 1];
                reader.read_exact(&mut buf)?;
                Ok(Tag::Byte(buf[0] as i8))
            }
            TagType::Short => {
                let mut buf = [0u8; 2];
                reader.read_exact(&mut buf)?;
                Ok(Tag::Short(i16::from_be_bytes(buf)))
            }
            TagType::Int => {
                let mut buf = [0u8; 4];
                reader.read_exact(&mut buf)?;
                Ok(Tag::Int(i32::from_be_bytes(buf)))
            }
            TagType::Long => {
                let mut buf = [0u8; 8];
                reader.read_exact(&mut buf)?;
                Ok(Tag::Long(i64::from_be_bytes(buf)))
            }
            TagType::Float => {
                let mut buf = [0u8; 4];
                reader.read_exact(&mut buf)?;
                Ok(Tag::Float(f32::from_be_bytes(buf)))
            }
            TagType::Double => {
                let mut buf = [0u8; 8];
                reader.read_exact(&mut buf)?;
                Ok(Tag::Double(f64::from_be_bytes(buf)))
            }
            TagType::ByteArray => {
                let mut len_buf = [0u8; 4];
                reader.read_exact(&mut len_buf)?;
                let len = i32::from_be_bytes(len_buf) as usize;
                let mut data = vec![0u8; len];
                reader.read_exact(&mut data)?;
                Ok(Tag::ByteArray(data))
            }
            TagType::String => {
                let mut len_buf = [0u8; 2];
                reader.read_exact(&mut len_buf)?;
                let len = u16::from_be_bytes(len_buf) as usize;
                let mut data = vec![0u8; len];
                reader.read_exact(&mut data)?;
                Ok(Tag::String(String::from_utf8(data).map_err(|e| {
                    io::Error::new(io::ErrorKind::InvalidData, e)
                })?))
            }
            TagType::List => {
                let mut ty_buf = [0u8; 1];
                reader.read_exact(&mut ty_buf)?;
                let ty = TagType::from_u8(ty_buf[0]);
                let mut len_buf = [0u8; 4];
                reader.read_exact(&mut len_buf)?;
                let len = i32::from_be_bytes(len_buf) as usize;
                let mut elements = Vec::with_capacity(len);
                for _ in 0..len {
                    elements.push(Self::read(reader, ty)?);
                }
                Ok(Tag::List(ty, elements))
            }
            TagType::Compound => {
                let mut map = HashMap::new();
                loop {
                    let mut type_buf = [0u8; 1];
                    reader.read_exact(&mut type_buf)?;
                    let type_id = type_buf[0];
                    if type_id == TagType::End as u8 {
                        break;
                    }
                    let tag_type = TagType::from_u8(type_id);
                    // Read string
                    let mut len_buf = [0u8; 2];
                    reader.read_exact(&mut len_buf)?;
                    let len = u16::from_be_bytes(len_buf) as usize;
                    let mut name_data = vec![0u8; len];
                    reader.read_exact(&mut name_data)?;
                    let name = String::from_utf8(name_data)
                        .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;

                    let tag = Self::read(reader, tag_type)?;
                    map.insert(name, tag);
                }
                Ok(Tag::Compound(map))
            }
        }
    }

    pub fn write_named<W: Write>(tag: &Tag, name: &str, writer: &mut W) -> io::Result<()> {
        writer.write_all(&[tag.id()])?;
        if tag.id() == TagType::End as u8 {
            return Ok(());
        }
        let name_bytes = name.as_bytes();
        writer.write_all(&(name_bytes.len() as u16).to_be_bytes())?;
        writer.write_all(name_bytes)?;
        tag.write(writer)
    }

    pub fn read_named<R: Read>(reader: &mut R) -> io::Result<(String, Tag)> {
        let mut type_buf = [0u8; 1];
        reader.read_exact(&mut type_buf)?;
        let type_id = type_buf[0];
        if type_id == TagType::End as u8 {
            return Ok((String::new(), Tag::End));
        }
        let tag_type = TagType::from_u8(type_id);

        let mut len_buf = [0u8; 2];
        reader.read_exact(&mut len_buf)?;
        let len = u16::from_be_bytes(len_buf) as usize;
        let mut name_data = vec![0u8; len];
        reader.read_exact(&mut name_data)?;
        let name = String::from_utf8(name_data)
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;

        let tag = Self::read(reader, tag_type)?;
        Ok((name, tag))
    }
}
