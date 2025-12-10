<template>
	<view class="container">
		<!-- Settings Button -->
		<view class="settings-btn" @tap="showConfigDialog">
			<text class="settings-icon">⚙️</text>
		</view>

		<!-- MODE Title and Toggle -->
		<view class="mode-section">
			<text class="mode-title">MODE</text>
			<view class="mode-toggle">
				<text 
					:class="['mode-option', deviceStatus.autoMode ? 'active' : 'inactive']"
					@tap="setMode(true)"
				>
					Automatic
				</text>
				<text 
					:class="['mode-option', !deviceStatus.autoMode ? 'active' : 'inactive']"
					@tap="setMode(false)"
				>
					Manual
				</text>
			</view>
		</view>

		<!-- Lamp Switch -->
		<view class="lamp-section">
			<text class="lamp-label">Lamp</text>
			<switch 
				class="lamp-switch" 
				:checked="deviceStatus.lightOn" 
				@change="onLampSwitchChange"
				:disabled="deviceStatus.autoMode"
				color="#000000"
			/>
			<text class="lamp-status">{{ deviceStatus.lightOn ? 'On' : 'Off' }}</text>
		</view>

		<!-- Semicircle Brightness Slider -->
		<view class="brightness-arc-container">
			<canvas 
				class="brightness-canvas" 
				canvas-id="brightnessArc"
				@touchstart="onArcTouchStart"
				@touchmove="onArcTouchMove"
				@touchend="onArcTouchEnd"
			></canvas>
			<view class="brightness-display">
				<text class="brightness-value">{{ currentBrightness }}%</text>
				<text class="brightness-text">Brightness</text>
			</view>
			<text class="brightness-min">Low</text>
			<text class="brightness-max">High</text>
		</view>

		<!-- Sensor Data (Auto Mode Only) -->
		<view class="sensor-section" v-if="deviceStatus.autoMode">
			<view class="sensor-item">
				<text class="sensor-label">People:</text>
				<text class="sensor-value">{{ deviceStatus.motion ? 'present' : 'absent' }}</text>
			</view>
			<view class="sensor-item">
				<text class="sensor-label">Light intensity:</text>
				<text class="sensor-value">{{ deviceStatus.lightValue }}</text>
			</view>
		</view>

		<!-- Configuration Dialog -->
		<view class="modal" v-if="showConfig" @tap="hideConfigModal">
			<view class="modal-content" @tap.stop>
				<view class="modal-title">Device Settings</view>
				<view class="config-section">
					<text class="section-title">Device IP Address</text>
					<input 
						class="modal-input" 
						v-model="tempIP" 
						placeholder="Enter ESP32 IP address"
					/>
					<view class="connection-status-row">
						<text class="status-label">Status:</text>
						<text :class="['status-text', isConnected ? 'connected' : 'disconnected']">
							{{ isConnected ? 'Connected' : 'Disconnected' }}
						</text>
					</view>
					<button class="test-btn" @tap="testConnectionDialog">Test Connection</button>
				</view>
				<view class="modal-buttons">
					<button class="modal-btn cancel" @tap="hideConfigModal">Cancel</button>
					<button class="modal-btn confirm" @tap="saveConfig">Save</button>
				</view>
			</view>
		</view>
	</view>
</template>

<script>
	export default {
		data() {
			return {
				deviceIP: '192.168.1.100',
				tempIP: '',
				isConnected: false,
				showConfig: false,
				updateTime: '',
				timer: null,
				deviceStatus: {
					lightOn: false,
					autoMode: true,
					lightValue: 0,
					motion: false,
					red: 255,
					green: 255,
					blue: 255,
					brightness: 100
				},
				currentBrightness: 100,
				canvasWidth: 0,
				canvasHeight: 0,
				centerX: 0,
				centerY: 0,
				radius: 0,
				ctx: null,
				isDragging: false
			}
		},
		onLoad() {
			// Read IP from local storage
			const savedIP = uni.getStorageSync('deviceIP')
			if (savedIP) {
				this.deviceIP = savedIP
			}
			
			// Start status update
			this.updateStatus()
			this.startAutoRefresh()
			
			// Initialize canvas
			this.$nextTick(() => {
				this.initCanvas()
			})
		},
		onUnload() {
			this.stopAutoRefresh()
		},
		methods: {
			// Update device status
			updateStatus() {
				uni.request({
					url: `http://${this.deviceIP}/status`,
					method: 'GET',
					timeout: 3000,
					success: (res) => {
						if (res.statusCode === 200) {
							// Field name mapping: ESP32 uses snake_case, frontend uses camelCase
							const mappedData = {
								lightOn: res.data.light_on,
								autoMode: res.data.auto_mode,
								lightValue: res.data.light_value,
								motion: res.data.motion,
								red: res.data.red,
								green: res.data.green,
								blue: res.data.blue,
								brightness: res.data.brightness
							}
							
							// Update device status
							this.deviceStatus = {
								...this.deviceStatus,
								...mappedData
							}
							this.isConnected = true
							this.updateTime = this.formatTime(new Date())
							
							// Sync brightness value
							if (res.data.brightness !== undefined) {
								this.currentBrightness = res.data.brightness
								this.drawArc()
							}
							
							console.log('ESP32 Raw Data:', res.data)
							console.log('Mapped Data:', mappedData)
						}
					},
					fail: (err) => {
						console.error('Failed to get status:', err)
						this.isConnected = false
					}
				})
			},
			
			// Set mode
			setMode(isAuto) {
				uni.request({
					url: `http://${this.deviceIP}/control`,
					method: 'POST',
					data: { action: 'toggle_mode' },
					header: {
						'Content-Type': 'application/json'
					},
					timeout: 3000,
					success: (res) => {
						uni.showToast({
							title: isAuto ? 'Switched to Auto Mode' : 'Switched to Manual Mode',
							icon: 'success'
						})
						this.updateStatus()
					},
					fail: () => {
						uni.showToast({
							title: 'Setting Failed',
							icon: 'none'
						})
					}
				})
			},
			
			// Test Connection (in Config Dialog)
			testConnectionDialog() {
				if (!this.tempIP) {
					uni.showToast({
						title: 'Please enter IP address',
						icon: 'none'
					})
					return
				}
				
				uni.showLoading({ title: 'Testing connection...' })
				
				uni.request({
					url: `http://${this.tempIP}/status`,
					method: 'GET',
					timeout: 3000,
					success: (res) => {
						uni.hideLoading()
						if (res.statusCode === 200) {
							uni.showModal({
								title: 'Connection Successful',
								content: `Device IP: ${this.tempIP}\nLight Status: ${res.data.light_on ? 'On' : 'Off'}\nMode: ${res.data.auto_mode ? 'Auto' : 'Manual'}`,
								showCancel: false
							})
						} else {
							uni.showToast({
								title: `HTTP ${res.statusCode}`,
								icon: 'none'
							})
						}
					},
					fail: (err) => {
						uni.hideLoading()
						uni.showModal({
							title: 'Connection Failed',
							content: 'Please check:\n1. Is Device IP correct?\n2. Is device powered on?\n3. Are they on the same WiFi?',
							showCancel: false
						})
					}
				})
			},
			
			// Control light
			controlLight(turnOn) {
				if (this.deviceStatus.autoMode) {
					uni.showToast({
						title: 'Cannot control manually in Auto mode',
						icon: 'none'
					})
					return
				}
				
				uni.request({
					url: `http://${this.deviceIP}/control`,
					method: 'POST',
					data: { action: turnOn ? 'on' : 'off' },
					header: {
						'Content-Type': 'application/json'
					},
					timeout: 3000,
					success: (res) => {
						uni.showToast({
							title: turnOn ? 'Light Turned On' : 'Light Turned Off',
							icon: 'success'
						})
						this.updateStatus()
					},
					fail: () => {
						uni.showToast({
							title: 'Control Failed',
							icon: 'none'
						})
					}
				})
			},
			
			// Show config dialog
			showConfigDialog() {
				this.tempIP = this.deviceIP
				this.showConfig = true
			},
			
			// Hide config dialog
			hideConfigModal() {
				this.showConfig = false
			},
			
			// Save config
			saveConfig() {
				if (!this.tempIP) {
					uni.showToast({
						title: 'Please enter IP address',
						icon: 'none'
					})
					return
				}
				
				this.deviceIP = this.tempIP
				uni.setStorageSync('deviceIP', this.deviceIP)
				this.showConfig = false
				
				uni.showToast({
					title: 'Config Saved',
					icon: 'success'
				})
				
				// Reconnect
				this.isConnected = false
				this.updateStatus()
			},
			
			// Start auto refresh
			startAutoRefresh() {
				this.timer = setInterval(() => {
					this.updateStatus()
				}, 1000)
			},
			
			// Stop auto refresh
			stopAutoRefresh() {
				if (this.timer) {
					clearInterval(this.timer)
					this.timer = null
				}
			},
			
			// Format time
			formatTime(date) {
				const h = date.getHours().toString().padStart(2, '0')
				const m = date.getMinutes().toString().padStart(2, '0')
				const s = date.getSeconds().toString().padStart(2, '0')
				return `${h}:${m}:${s}`
			},
			
			// Lamp switch change
			onLampSwitchChange(e) {
				if (this.deviceStatus.autoMode) {
					uni.showToast({
						title: 'Cannot control in Auto mode',
						icon: 'none'
					})
					return
				}
				const turnOn = e.detail.value
				this.controlLight(turnOn)
			},
			
			// Initialize canvas
			initCanvas() {
				const query = uni.createSelectorQuery().in(this)
				query.select('.brightness-canvas').boundingClientRect(data => {
					if (data) {
						this.canvasWidth = data.width
						this.canvasHeight = data.height
						// Center on the right middle
						this.centerX = this.canvasWidth - 50
						this.centerY = this.canvasHeight / 2
						this.radius = Math.min(this.canvasWidth * 0.8, this.canvasHeight * 0.6)
						
						this.ctx = uni.createCanvasContext('brightnessArc', this)
						this.drawArc()
					}
				}).exec()
			},
			
			// Draw semicircle arc
			drawArc() {
				if (!this.ctx) return
				
				const ctx = this.ctx
				// From top-left (High) to bottom-left (Low), center on the right
				const startAngle = Math.PI * 3 / 4  // 135 deg (top left)
				const endAngle = Math.PI * 5 / 4     // 225 deg (bottom left)
				// Calculate current angle (top to bottom)
				const angleRange = endAngle - startAngle
				const currentAngle = startAngle + angleRange * (this.currentBrightness / 100)
				
				// Clear canvas
				ctx.clearRect(0, 0, this.canvasWidth, this.canvasHeight)
				
				// Draw background arc (gray) - from top-left to bottom-left
				ctx.beginPath()
				ctx.arc(this.centerX, this.centerY, this.radius, startAngle, endAngle, false)
				ctx.setLineWidth(15)
				ctx.setStrokeStyle('#E5E5E5')
				ctx.stroke()
				
				// Draw brightness arc (black)
				if (this.currentBrightness > 0) {
					ctx.beginPath()
					ctx.arc(this.centerX, this.centerY, this.radius, startAngle, currentAngle, false)
					ctx.setLineWidth(15)
					ctx.setStrokeStyle('#333333')
					ctx.stroke()
				}
				
				// Draw control point
				const dotX = this.centerX + this.radius * Math.cos(currentAngle)
				const dotY = this.centerY + this.radius * Math.sin(currentAngle)
				
				ctx.beginPath()
				ctx.arc(dotX, dotY, 20, 0, 2 * Math.PI)
				ctx.setFillStyle('#333333')
				ctx.fill()
				
				// Draw inner circle
				ctx.beginPath()
				ctx.arc(dotX, dotY, 10, 0, 2 * Math.PI)
				ctx.setFillStyle('#FFFFFF')
				ctx.fill()
				
				ctx.draw()
			},
			
			// Touch start
			onArcTouchStart(e) {
				this.isDragging = true
				this.handleArcTouch(e)
			},
			
			// Touch move
			onArcTouchMove(e) {
				if (this.isDragging) {
					this.handleArcTouch(e)
				}
			},
			
			// Touch end
			onArcTouchEnd(e) {
				this.isDragging = false
				// Send brightness setting command
				this.setBrightness(this.currentBrightness)
			},
			
			// Handle touch event
			handleArcTouch(e) {
				const touch = e.touches[0]
				const x = touch.x
				const y = touch.y
				
				// Calculate angle
				const dx = x - this.centerX
				const dy = y - this.centerY
				let angle = Math.atan2(dy, dx)
				
				// Convert angle to 0-2π range
				if (angle < 0) angle += 2 * Math.PI
				
				// Only process left semicircle (135 deg to 225 deg, i.e., 3π/4 to 5π/4)
				const startAngle = Math.PI * 3 / 4
				const endAngle = Math.PI * 5 / 4
				
				if (angle >= startAngle && angle <= endAngle) {
					const percent = (angle - startAngle) / (endAngle - startAngle)
					this.currentBrightness = Math.round(percent * 100)
					this.currentBrightness = Math.max(1, Math.min(100, this.currentBrightness))
					this.drawArc()
				}
			},
			
			// Set brightness (send to device)
			setBrightness(brightness) {
				uni.request({
					url: `http://${this.deviceIP}/control`,
					method: 'POST',
					data: { 
						action: 'set_brightness',
						brightness: parseInt(brightness)
					},
					header: {
						'Content-Type': 'application/json'
					},
					timeout: 3000,
					success: (res) => {
						this.updateStatus()
					},
					fail: () => {
						uni.showToast({
							title: 'Failed to set brightness',
							icon: 'none'
						})
					}
				})
			}
		}
	}
</script>

<style lang="scss" scoped>
.container {
	background: #F5F5F5;
	min-height: 100vh;
	padding: 40rpx;
	position: relative;
}

// Settings Button
.settings-btn {
	position: absolute;
	top: 40rpx;
	right: 40rpx;
	width: 80rpx;
	height: 80rpx;
	display: flex;
	align-items: center;
	justify-content: center;
	background: white;
	border-radius: 50%;
	box-shadow: 0 4rpx 12rpx rgba(0, 0, 0, 0.1);
	z-index: 100;
	
	.settings-icon {
		font-size: 40rpx;
	}
}

// MODE Section
.mode-section {
	margin-bottom: 50rpx;
	
	.mode-title {
		display: block;
		font-size: 28rpx;
		color: #999;
		margin-bottom: 15rpx;
		font-weight: 500;
		letter-spacing: 2rpx;
	}
	
	.mode-toggle {
		display: flex;
		gap: 30rpx;
		
		.mode-option {
			font-size: 48rpx;
			font-weight: bold;
			cursor: pointer;
			transition: all 0.3s;
			
			&.active {
				color: #000;
			}
			
			&.inactive {
				color: #CCCCCC;
			}
		}
	}
}

// Lamp Switch Section
.lamp-section {
	display: flex;
	align-items: center;
	margin-bottom: 80rpx;
	
	.lamp-label {
		font-size: 32rpx;
		color: #333;
		font-weight: 600;
		margin-right: 20rpx;
	}
	
	.lamp-switch {
		margin-right: 15rpx;
	}
	
	.lamp-status {
		font-size: 24rpx;
		color: #999;
	}
}

// Semicircle Brightness Controller
.brightness-arc-container {
	position: relative;
	width: 100%;
	height: 500rpx;
	margin-bottom: 100rpx;
	
	.brightness-canvas {
		width: 100%;
		height: 100%;
	}
	
	.brightness-display {
		position: absolute;
		top: 50%;
		right: 80rpx;
		transform: translateY(-50%);
		text-align: center;
		
		.brightness-value {
			display: block;
			font-size: 80rpx;
			font-weight: bold;
			color: #333;
			margin-bottom: 10rpx;
		}
		
		.brightness-text {
			display: block;
			font-size: 28rpx;
			color: #999;
		}
	}
	
	.brightness-min {
		position: absolute;
		bottom: 40rpx;
		left: 20rpx;
		font-size: 24rpx;
		color: #999;
	}
	
	.brightness-max {
		position: absolute;
		top: 40rpx;
		left: 20rpx;
		font-size: 24rpx;
		color: #999;
	}
}

// Sensor Data Section
.sensor-section {
	margin-top: 60rpx;
	
	.sensor-item {
		display: flex;
		align-items: baseline;
		margin-bottom: 20rpx;
		
		.sensor-label {
			font-size: 28rpx;
			color: #999;
			margin-right: 15rpx;
		}
		
		.sensor-value {
			font-size: 32rpx;
			color: #333;
			font-weight: 600;
		}
	}
}

// Modal Dialog
.modal {
	position: fixed;
	top: 0;
	left: 0;
	right: 0;
	bottom: 0;
	background: rgba(0, 0, 0, 0.5);
	display: flex;
	align-items: center;
	justify-content: center;
	z-index: 1000;
}

.modal-content {
	width: 600rpx;
	background: white;
	border-radius: 20rpx;
	padding: 40rpx;
	max-height: 80vh;
	overflow-y: auto;
}

.modal-title {
	font-size: 36rpx;
	font-weight: bold;
	color: #333;
	margin-bottom: 30rpx;
	text-align: center;
}

.config-section {
	margin-bottom: 30rpx;
	
	.section-title {
		display: block;
		font-size: 28rpx;
		color: #666;
		margin-bottom: 15rpx;
		font-weight: 600;
	}
	
	.modal-input {
		width: 100%;
		height: 80rpx;
		border: 2rpx solid #e0e0e0;
		border-radius: 10rpx;
		padding: 0 20rpx;
		font-size: 28rpx;
		margin-bottom: 20rpx;
	}
	
	.connection-status-row {
		display: flex;
		align-items: center;
		margin-bottom: 20rpx;
		
		.status-label {
			font-size: 26rpx;
			color: #666;
			margin-right: 15rpx;
		}
		
		.status-text {
			font-size: 26rpx;
			font-weight: 600;
			
			&.connected {
				color: #4CAF50;
			}
			
			&.disconnected {
				color: #F44336;
			}
		}
	}
}

.test-btn {
	width: 100%;
	height: 70rpx;
	line-height: 70rpx;
	background: #333;
	color: white;
	border: none;
	border-radius: 10rpx;
	font-size: 28rpx;
	transition: all 0.3s;
	
	&:active {
		opacity: 0.8;
	}
}

.modal-buttons {
	display: flex;
	gap: 20rpx;
	
	.modal-btn {
		flex: 1;
		height: 70rpx;
		line-height: 70rpx;
		border-radius: 10rpx;
		font-size: 28rpx;
		border: none;
		
		&.cancel {
			background: #f0f0f0;
			color: #666;
		}
		
		&.confirm {
			background: #333;
			color: white;
		}
	}
}
</style>