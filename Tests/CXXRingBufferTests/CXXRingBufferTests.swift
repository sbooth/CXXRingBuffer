import Testing
import Foundation
@testable import CXXRingBuffer

@MainActor
@Suite struct CXXRingBufferTests {
	@Test func empty() {
		var rb = CXXRingBuffer.SFB.RingBuffer()

		#expect(rb.CapacityBytes() == 0)
		#expect(rb.BytesAvailableToRead() == 0)
		#expect(rb.BytesAvailableToWrite() == 0)

		var d = Data(capacity: 1024)
		d.withUnsafeMutableBytes { (ptr: UnsafeMutableRawBufferPointer) in
			#expect(rb.Read(ptr.baseAddress!, 1024) == 0)
			#expect(rb.Write(ptr.baseAddress!, 1024) == 0)
		}
	}

	@Test func capacity() {
		var rb = CXXRingBuffer.SFB.RingBuffer()

		#expect(rb.Allocate(1) == false)
		#expect(rb.Allocate(0x80000000) == false)

		#expect(rb.Allocate(2) == true)
		#expect(rb.Allocate(0x7FFFFFFF) == true)

		#expect(rb.Allocate(1024) == true)
		#expect(rb.CapacityBytes() >= 1024)
		#expect(rb.BytesAvailableToRead() == 0)
		#expect(rb.BytesAvailableToWrite() >= 1024)
	}

	@Test func basic() {
		var rb = CXXRingBuffer.SFB.RingBuffer()

		#expect(rb.Allocate(128) == true)

		let written = Data(stride(from: 0, through: 15, by: 1))
		written.withUnsafeBytes { (ptr: UnsafeRawBufferPointer) in
			#expect(rb.Write(ptr.baseAddress!, UInt32(written.count)) == 16)
		}
		#expect(rb.BytesAvailableToRead() == written.count)

		var read = Data(count: written.count)
		read.withUnsafeMutableBytes { (ptr: UnsafeMutableRawBufferPointer) in
			#expect(rb.Read(ptr.baseAddress!, 16) == 16)
		}

		#expect(read == written)
		#expect(rb.BytesAvailableToRead() == 0)
	}
}
