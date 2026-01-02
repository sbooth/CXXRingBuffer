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
		#expect(rb.Capacity() == 0)
		#expect(rb.AvailableBytes() == 0)
		#expect(rb.FreeSpace() == rb.Capacity())

		var d = Data(capacity: 1024)
		d.withUnsafeMutableBytes { (ptr: UnsafeMutableRawBufferPointer) in
			#expect(rb.Read(ptr.baseAddress!, 1, 1024, true) == 0)
			#expect(rb.Write(ptr.baseAddress!, 1, 1024, true) == 0)
		}
	}

	@Test func nonempty() async {
		var rb = CXXRingBuffer.RingBuffer()

		let capacity = 256
		#expect(rb.Allocate(capacity) == true)
		#expect(rb.__convertToBool() == true)
		#expect(rb.Capacity() == capacity)
		#expect(rb.AvailableBytes() == 0)
		#expect(rb.FreeSpace() == rb.Capacity())

		rb.Deallocate()
		#expect(rb.__convertToBool() == false)
		#expect(rb.Capacity() == 0)
		#expect(rb.AvailableBytes() == 0)
		#expect(rb.FreeSpace() == rb.Capacity())
	}

	@Test func capacity() async {
		var rb = CXXRingBuffer.RingBuffer()

		#expect(rb.Allocate(1) == false)
		#expect(rb.Allocate(2) == true)
		#expect(rb.Allocate(1024) == true)
	}

	@Test func drain() async {
		var rb = CXXRingBuffer.RingBuffer()

		#expect(rb.Allocate(128) == true)

		#expect(rb.Drain() == 0)

		let x = 0
		#expect(rb.WriteValue(x) == true)
		#expect(rb.Drain() == MemoryLayout.stride(ofValue: x))

		#expect(rb.Drain() == 0)
	}

	@Test func basic() async {
		var rb = CXXRingBuffer.RingBuffer()

		#expect(rb.Allocate(128) == true)

		let written = Data(stride(from: 0, through: 15, by: 1))
		written.withUnsafeBytes { (ptr: UnsafeRawBufferPointer) in
			#expect(rb.Write(ptr.baseAddress!, 1, written.count, true) == 16)
		}
		#expect(rb.AvailableBytes() == written.count)

		var read = Data(count: written.count)
		read.withUnsafeMutableBytes { (ptr: UnsafeMutableRawBufferPointer) in
			#expect(rb.Read(ptr.baseAddress!, 1, 16, true) == 16)
		}

		#expect(read == written)
		#expect(rb.AvailableBytes() == 0)
	}

	@Test func multi() async {
		var rb = CXXRingBuffer.RingBuffer()

		let data_size = 255
		let buf_size = 128
		#expect(rb.Allocate(buf_size) == true)

		let producer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: Int(data_size))
		let consumer_buf = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: Int(data_size))

		arc4random_buf(producer_buf.baseAddress, Int(data_size))

		var written = 0
		var read = 0

		var addr = producer_buf.baseAddress
		var length = rb.Write(addr!, 1, 64, true)
		written += length
		#expect(length == 64)
		#expect(rb.AvailableBytes() == 64)
		#expect(rb.FreeSpace() == rb.Capacity() - 64)

		addr = consumer_buf.baseAddress
		length = rb.Read(addr!, 1, 64, true)
		read += length
		#expect(length == 64)
		#expect(rb.AvailableBytes() == 0)
		#expect(rb.FreeSpace() == rb.Capacity())

		addr = producer_buf.baseAddress?.advanced(by: Int(written))
		length = rb.Write(addr!, 1, 64, true)
		written += length
		#expect(length == 64)
		#expect(rb.AvailableBytes() == 64)
		#expect(rb.FreeSpace() == rb.Capacity() - 64)

		addr = consumer_buf.baseAddress?.advanced(by: Int(read))
		length = rb.Read(addr!, 1, 64, true)
		read += length
		#expect(length == 64)
		#expect(rb.AvailableBytes() == 0)
		#expect(rb.FreeSpace() == rb.Capacity())

		addr = producer_buf.baseAddress?.advanced(by: Int(written))
		length = rb.Write(addr!, 1, 127, true)
		written += length
		#expect(length == 127)
		#expect(rb.AvailableBytes() == 127)
		#expect(rb.FreeSpace() == rb.Capacity() - 127)

		addr = consumer_buf.baseAddress?.advanced(by: Int(read))
		length = rb.Read(addr!, 1, 127, true)
		read += length
		#expect(length == 127)
		#expect(rb.AvailableBytes() == 0)
		#expect(rb.FreeSpace() == rb.Capacity())

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
		#expect(rb.Allocate(buf_size) == true)

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
				let length = rb.Write(addr!, 1, n, true)
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
				let length = rb.Read(addr!, 1, n, true)
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
