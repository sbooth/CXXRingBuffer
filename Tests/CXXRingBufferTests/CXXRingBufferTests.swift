import Testing
import Foundation
@testable import CXXRingBuffer

@Suite struct CXXRingBufferTests {
	@Test func empty() async {
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

	@Test func capacity() async {
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

	@Test func basic() async {
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

	@Test func spsc() {
		var rb = CXXRingBuffer.SFB.RingBuffer()

		let size = 8192
		#expect(rb.Allocate(UInt32(size / 4)) == true)

		let producer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: size)
		let consumer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: size)

		arc4random_buf(producer_buf.baseAddress, size);

		let producer = DispatchQueue(label: "producer")
		let consumer = DispatchQueue(label: "consumer")

		let group = DispatchGroup()

		producer.async(group: group) {
			var remaining = UInt32(size)
			var written: UInt32 = 0

			while remaining > 0 {
				let n = UInt32.random(in: 0...remaining)
				let addr = producer_buf.baseAddress?.advanced(by: Int(written))
				let length = rb.Write(addr!, n)
				remaining -= length
				written += length
				usleep(useconds_t.random(in: 0..<10))
			}
		}

		consumer.async(group: group) {
			var remaining = UInt32(size)
			var read: UInt32 = 0

			while remaining > 0 {
				let n = UInt32.random(in: 0...remaining)
				let addr = consumer_buf.baseAddress?.advanced(by: Int(read))
				let length = rb.Read(addr!, n)
				remaining -= length
				read += length
				usleep(useconds_t.random(in: 0..<10))
			}
		}

		group.wait()

		#expect(memcmp(producer_buf.baseAddress, consumer_buf.baseAddress, size) == 0)

		producer_buf.deallocate()
		consumer_buf.deallocate()
	}
}
