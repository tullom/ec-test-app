// Sample Definition of FAN ACPI

Device (FAN0) {
  Name (_HID, EISAID ("PNP0C0B"))
  
  Method (_FIF, 0, Serialized) {
     Return (Package() { 0x0, 0x0, 0x10, 0x1})
  }

  Method (_FPS) {
    Return (Package () { 0x0, package() {0x1,0x2,0x3,0x4,0x5}})
  }

  Method (_FSL, 1, NotSerialized) {
    
  }

  Method (_FST) {
    Return (Package() {0x0,0x1,0x2})
  }

}
