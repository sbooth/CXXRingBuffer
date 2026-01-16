//
// SPDX-FileCopyrightText: 2025 Stephen F. Booth <sbooth@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXRingBuffer
//

import Testing
import Foundation
@testable import CXXRingBuffer

@Suite struct CXXRingBufferTests {
	@Test func empty() async {
		var rb = CXXRingBuffer.RingBuffer()

		#expect(rb.__convertToBool() == false)
		#expect(rb.capacity() == 0)
		#expect(rb.availableBytes() == 0)
		#expect(rb.freeSpace() == rb.capacity())

		var d = Data(capacity: 1024)
		d.withUnsafeMutableBytes { (ptr: UnsafeMutableRawBufferPointer) in
			#expect(rb.read(ptr.baseAddress!, 1, 1024, true) == 0)
			#expect(rb.write(ptr.baseAddress!, 1, 1024, true) == 0)
		}
	}

	@Test func nonempty() async {
		var rb = CXXRingBuffer.RingBuffer()

		let capacity = 256
		#expect(rb.allocate(capacity) == true)
		#expect(rb.__convertToBool() == true)
		#expect(rb.capacity() == capacity)
		#expect(rb.availableBytes() == 0)
		#expect(rb.freeSpace() == rb.capacity())

		rb.deallocate()
		#expect(rb.__convertToBool() == false)
		#expect(rb.capacity() == 0)
		#expect(rb.availableBytes() == 0)
		#expect(rb.freeSpace() == rb.capacity())
	}

	@Test func capacity() async {
		var rb = CXXRingBuffer.RingBuffer()

		#expect(rb.allocate(1) == false)
		#expect(rb.allocate(2) == true)
		#expect(rb.allocate(1024) == true)
	}

	@Test func drain() async {
		var rb = CXXRingBuffer.RingBuffer()

		#expect(rb.allocate(128) == true)

		#expect(rb.drain() == 0)

		let x = 0
		#expect(rb.writeValue(x) == true)
		#expect(rb.drain() == MemoryLayout.stride(ofValue: x))

		#expect(rb.drain() == 0)
	}

	@Test func basic() async {
		var rb = CXXRingBuffer.RingBuffer()

		#expect(rb.allocate(128) == true)

		let written = Data(stride(from: 0, through: 15, by: 1))
		written.withUnsafeBytes { (ptr: UnsafeRawBufferPointer) in
			#expect(rb.write(ptr.baseAddress!, 1, written.count, true) == 16)
		}
		#expect(rb.availableBytes() == written.count)

		var read = Data(count: written.count)
		read.withUnsafeMutableBytes { (ptr: UnsafeMutableRawBufferPointer) in
			#expect(rb.read(ptr.baseAddress!, 1, 16, true) == 16)
		}

		#expect(read == written)
		#expect(rb.availableBytes() == 0)
	}

	@Test func multi() async {
		var rb = CXXRingBuffer.RingBuffer()

		let data_size = 255
		let buf_size = 128
		#expect(rb.allocate(buf_size) == true)

		let producer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: Int(data_size))
		let consumer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: Int(data_size))

		arc4random_buf(producer_buf.baseAddress, Int(data_size))

		var written = 0
		var read = 0

		var addr = producer_buf.baseAddress
		var length = rb.write(addr!, 1, 64, true)
		written += length
		#expect(length == 64)
		#expect(rb.availableBytes() == 64)
		#expect(rb.freeSpace() == rb.capacity() - 64)

		addr = consumer_buf.baseAddress
		length = rb.read(addr!, 1, 64, true)
		read += length
		#expect(length == 64)
		#expect(rb.availableBytes() == 0)
		#expect(rb.freeSpace() == rb.capacity())

		addr = producer_buf.baseAddress?.advanced(by: Int(written))
		length = rb.write(addr!, 1, 64, true)
		written += length
		#expect(length == 64)
		#expect(rb.availableBytes() == 64)
		#expect(rb.freeSpace() == rb.capacity() - 64)

		addr = consumer_buf.baseAddress?.advanced(by: Int(read))
		length = rb.read(addr!, 1, 64, true)
		read += length
		#expect(length == 64)
		#expect(rb.availableBytes() == 0)
		#expect(rb.freeSpace() == rb.capacity())

		addr = producer_buf.baseAddress?.advanced(by: Int(written))
		length = rb.write(addr!, 1, 127, true)
		written += length
		#expect(length == 127)
		#expect(rb.availableBytes() == 127)
		#expect(rb.freeSpace() == rb.capacity() - 127)

		addr = consumer_buf.baseAddress?.advanced(by: Int(read))
		length = rb.read(addr!, 1, 127, true)
		read += length
		#expect(length == 127)
		#expect(rb.availableBytes() == 0)
		#expect(rb.freeSpace() == rb.capacity())

		#expect(written == data_size)
		#expect(read == data_size)
		#expect(memcmp(producer_buf.baseAddress, consumer_buf.baseAddress, 255) == 0)

		producer_buf.deallocate()
		consumer_buf.deallocate()
	}

	@Test func spsc() {
		var rb = CXXRingBuffer.RingBuffer()

		let data_size = 8192

		let buf_size = data_size / 4
		#expect(rb.allocate(buf_size) == true)

		let group = DispatchGroup()

		let producer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: Int(data_size))
		arc4random_buf(producer_buf.baseAddress, Int(data_size))

		let producer = DispatchQueue(label: "producer")
		producer.async(group: group) {
			var remaining = data_size
			var written = 0

			while remaining > 0 {
				let n = Int.random(in: 0...remaining)
				let addr = producer_buf.baseAddress?.advanced(by: Int(written))
				let length = rb.write(addr!, 1, n, true)
				remaining -= length
				written += length
				usleep(useconds_t.random(in: 0..<10))
			}
		}

		let consumer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: Int(data_size))

		let consumer = DispatchQueue(label: "consumer")
		consumer.async(group: group) {
			var remaining = data_size
			var read = 0

			while remaining > 0 {
				let n = Int.random(in: 0...remaining)
				let addr = consumer_buf.baseAddress?.advanced(by: Int(read))
				let length = rb.read(addr!, 1, n, true)
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
