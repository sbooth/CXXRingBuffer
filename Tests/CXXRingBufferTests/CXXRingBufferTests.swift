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
		#expect(rb.Allocate(2) == true)
		#expect(rb.Allocate(0x80000000) == true)
		#expect(rb.Allocate(0x80000001) == false)

		#expect(rb.Allocate(1024) == true)
		#expect(rb.CapacityBytes() == 1023)
		#expect(rb.BytesAvailableToRead() == 0)
		#expect(rb.BytesAvailableToWrite() == rb.CapacityBytes())
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

	@Test func multi() async {
		var rb = CXXRingBuffer.SFB.RingBuffer()

		let data_size: UInt32 = 255
		let buf_size: UInt32 = 128
		#expect(rb.Allocate(buf_size) == true)

		let producer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: Int(data_size))
		let consumer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: Int(data_size))

		arc4random_buf(producer_buf.baseAddress, Int(data_size))

		var written: UInt32 = 0
		var read: UInt32 = 0

		var addr = producer_buf.baseAddress
		var length = rb.Write(addr!, 64)
		written += length
		#expect(length == 64)
		#expect(rb.BytesAvailableToRead() == 64)
		#expect(rb.BytesAvailableToWrite() == rb.CapacityBytes() - 64)

		addr = consumer_buf.baseAddress
		length = rb.Read(addr!, 64)
		read += length
		#expect(length == 64)
		#expect(rb.BytesAvailableToRead() == 0)
		#expect(rb.BytesAvailableToWrite() == rb.CapacityBytes())

		addr = producer_buf.baseAddress?.advanced(by: Int(written))
		length = rb.Write(addr!, 64)
		written += length
		#expect(length == 64)
		#expect(rb.BytesAvailableToRead() == 64)
		#expect(rb.BytesAvailableToWrite() == rb.CapacityBytes() - 64)

		addr = consumer_buf.baseAddress?.advanced(by: Int(read))
		length = rb.Read(addr!, 64)
		read += length
		#expect(length == 64)
		#expect(rb.BytesAvailableToRead() == 0)
		#expect(rb.BytesAvailableToWrite() == rb.CapacityBytes())

		addr = producer_buf.baseAddress?.advanced(by: Int(written))
		length = rb.Write(addr!, 127)
		written += length
		#expect(length == 127)
		#expect(rb.BytesAvailableToRead() == 127)
		#expect(rb.BytesAvailableToWrite() == rb.CapacityBytes() - 127)

		addr = consumer_buf.baseAddress?.advanced(by: Int(read))
		length = rb.Read(addr!, 127)
		read += length
		#expect(length == 127)
		#expect(rb.BytesAvailableToRead() == 0)
		#expect(rb.BytesAvailableToWrite() == rb.CapacityBytes())

		#expect(written == data_size)
		#expect(read == data_size)
		#expect(memcmp(producer_buf.baseAddress, consumer_buf.baseAddress, 255) == 0)

		producer_buf.deallocate()
		consumer_buf.deallocate()
	}

	@Test func spsc() {
		var rb = CXXRingBuffer.SFB.RingBuffer()

		let data_size: UInt32 = 8192

		let buf_size = data_size / 4
		#expect(rb.Allocate(buf_size) == true)

		let group = DispatchGroup()

		let producer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: Int(data_size))
		arc4random_buf(producer_buf.baseAddress, Int(data_size))

		let producer = DispatchQueue(label: "producer")
		producer.async(group: group) {
			var remaining = data_size
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

		let consumer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: Int(data_size))

		let consumer = DispatchQueue(label: "consumer")
		consumer.async(group: group) {
			var remaining = data_size
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

		#expect(memcmp(producer_buf.baseAddress, consumer_buf.baseAddress, Int(data_size)) == 0)

		producer_buf.deallocate()
		consumer_buf.deallocate()
	}
}
