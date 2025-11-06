import Testing
@testable import CXXRingBuffer

@MainActor @Test func ringBufferTest() throws {
	var rb = CXXRingBuffer.SFB.RingBuffer()
	rb.Allocate(1024)
	#expect(rb.CapacityBytes() == 1024)
	#expect(rb.BytesAvailableToRead() == 0)
	#expect(rb.BytesAvailableToWrite() == 1023)
}
