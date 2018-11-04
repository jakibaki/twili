#pragma once

#include "platform.hpp"

#include<thread>
#include<list>
#include<queue>
#include<mutex>
#include<condition_variable>

#include<libusbk.h>

#include "Buffer.hpp"
#include "Device.hpp"
#include "Messages.hpp"
#include "Protocol.hpp"
#include "platform/windows/EventLoop.hpp"

namespace twili {
namespace twibd {

class Twibd;

namespace backend {

class USBKBackend {
 public:
	USBKBackend(Twibd &twibd);
	~USBKBackend();
	
	void Probe();

	void DetectDevice(KLST_DEVINFO_HANDLE device_info);
	void AddDevice(KLST_DEVINFO_HANDLE device_info);

 private:
	Twibd &twibd;
	
	KHOT_HANDLE hot_handle = nullptr;

	bool event_thread_destroy = false;
	void event_thread_func();
	std::thread event_thread;

	static void hotplug_notify_shim(KHOT_HANDLE hot_handle, KLST_DEVINFO_HANDLE device_info, KLST_SYNC_FLAG plug_type);

	class UsbHandle {
	public:
		UsbHandle();
		UsbHandle(KUSB_HANDLE handle);
		~UsbHandle();

		UsbHandle(UsbHandle &&other);
		UsbHandle(const UsbHandle &other) = delete;
		UsbHandle &operator=(UsbHandle &&other);
		UsbHandle &operator=(const UsbHandle &other) = delete;

		KUSB_HANDLE handle = nullptr;
	};

	class UsbOvlPool {
	public:
		UsbOvlPool(UsbHandle &handle, int max);
		~UsbOvlPool();

		KOVL_POOL_HANDLE handle = nullptr;
	};


	class Device : public twibd::Device, public std::enable_shared_from_this<Device> {
	 public:
		Device(USBKBackend &backend, KUSB_DRIVER_API Usb, UsbHandle &&handle, uint8_t ep_addrs[4]);
		~Device();

		enum class State {
			AVAILABLE, BUSY
		};

		void Begin();
		void AddMembers(platform::windows::EventLoop &loop);

		// thread-agnostic
		virtual void SendRequest(const Request &&r) override;

		virtual int GetPriority() override;
		virtual std::string GetBridgeType() override;
		
		bool ready_flag = false;
		bool added_flag = false;
	 private:
		USBKBackend &backend;
		KUSB_DRIVER_API Usb;

		UsbHandle handle;

		class TransferMember : public platform::windows::EventLoop::Member {
		public:
			TransferMember(Device &device, UsbOvlPool &pool, uint8_t ep_addr, void (Device::*callback)(size_t size));
			~TransferMember();

			void Submit(uint8_t *buffer, size_t size);
			virtual bool WantsSignal();
			virtual void Signal();
			virtual platform::windows::Event &GetEvent();
		private:
			Device &device;
			uint8_t ep_addr;
			void (Device::*callback)(size_t);

			KOVL_HANDLE overlap;
			platform::windows::Event event;

			bool is_active = false;
		};

		UsbOvlPool pool;

		TransferMember member_meta_out;
		TransferMember member_meta_in;
		TransferMember member_data_out;
		TransferMember member_data_in;

		std::mutex state_mutex;
		std::condition_variable state_cv;
		State state = State::AVAILABLE;

		protocol::MessageHeader mhdr_in;
		protocol::MessageHeader mhdr;
		WeakRequest request_out;
		Response response_in;
		std::list<WeakRequest> pending_requests;
		std::vector<uint32_t> object_ids_in;

		bool transferring_meta;
		bool transferring_data;
		size_t data_out_transferred;
		size_t data_in_transferred;
		bool read_in_objects;

		void MetaOutTransferCompleted(size_t size);
		void MetaInTransferCompleted(size_t size);
		void DataOutTransferCompleted(size_t size);
		void DataInTransferCompleted(size_t size);

		void ResubmitMetaInTransfer();
		void DispatchResponse();
		void Identified(Response &r);

		static size_t LimitTransferSize(size_t size);
	};

	class StdoutTransferState : public platform::windows::EventLoop::Member {
	public:
		StdoutTransferState(USBKBackend &backend, KUSB_DRIVER_API Usb, UsbHandle &&handle, uint8_t ep_addr);
		~StdoutTransferState();

		void Submit();
		void Kill();
		
		virtual bool WantsSignal();
		virtual void Signal();
		virtual platform::windows::Event &GetEvent();

		bool deletion_flag = false;
	private:
		USBKBackend &backend;
		KUSB_DRIVER_API Usb;
		UsbHandle handle;
		uint8_t ep_addr;

		KOVL_POOL_HANDLE pool;
		KOVL_HANDLE overlap;

		platform::windows::Event event;

		uint8_t io_buffer[512];
		util::Buffer string_buffer;
	};

	class Logic : public platform::windows::EventLoop::Logic {
	public:
		Logic(USBKBackend &backend);
		virtual void Prepare(platform::windows::EventLoop &server) override;
	private:
		USBKBackend &backend;
	} logic;

	void AddTwiliDevice(KLST_DEVINFO_HANDLE device_info);
	void AddSerialConsole(KLST_DEVINFO_HANDLE device_info);

	std::list<std::shared_ptr<Device>> devices;
	std::list<std::shared_ptr<StdoutTransferState>> stdout_transfers;

	platform::windows::EventLoop event_loop;
};

} // namespace backend
} // namespace twibd
} // namespace twili
