<script lang="uts">
  // #ifdef APP-ANDROID || APP-HARMONY
	let firstBackTime = 0
  // #endif
	export default {
		onLaunch: function () {
			console.log('App Launch')
		},
		onShow: function () {
			console.log('App Show')
		},
		onHide: function () {
			console.log('App Hide')
		},
		// #ifdef APP-ANDROID || APP-HARMONY
		onLastPageBackPress: function () {
			console.log('App LastPageBackPress')
			if (firstBackTime == 0) {
				uni.showToast({
					title: 'Press again to exit app',
					position: 'bottom',
				})
				firstBackTime = Date.now()
				setTimeout(() => {
					firstBackTime = 0
				}, 2000)
			} else if (Date.now() - firstBackTime < 2000) {
				firstBackTime = Date.now()
				uni.exit()
			}
		},
		// #endif
		onExit: function () {
			console.log('App Exit')
		},
	}
</script>

<style lang="scss">
	/* Common CSS for each page */
	.uni-row {
		flex-direction: row;
	}

	.uni-column {
		flex-direction: column;
	}
	
	/* ========================================
	   Smart Lighting System - Global Styles (Android Compatible)
	   Use rpx units to ensure normal display on mobile
	   ======================================== */
	
	/* Status Indicator - Glowing Dot */
	.indicator {
		width: 24rpx;
		height: 24rpx;
		border-radius: 50%;
		display: inline-block;
		margin-right: 16rpx;
		transition: all 0.3s ease;
	}
	
	.indicator.on {
		background: linear-gradient(135deg, #4ade80 0%, #22c55e 100%);
		box-shadow: 0 0 20rpx rgba(74, 222, 128, 0.6);
		animation: pulse-glow 2s ease-in-out infinite;
	}
	
	.indicator.off {
		background: #94a3b8;
		box-shadow: 0 2rpx 4rpx rgba(0, 0, 0, 0.1);
	}
	
	@keyframes pulse-glow {
		0%, 100% {
			box-shadow: 0 0 20rpx rgba(74, 222, 128, 0.6);
		}
		50% {
			box-shadow: 0 0 32rpx rgba(74, 222, 128, 0.9);
		}
	}
	
	/* Card Styles */
	.status-card, .control-card, .mode-card, .connection-card {
		background: #ffffff;
		border-radius: 32rpx;
		padding: 32rpx;
		margin: 24rpx;
		box-shadow: 0 4rpx 16rpx rgba(0, 0, 0, 0.08);
	}
	
	.card-title {
		font-size: 36rpx;
		font-weight: 700;
		margin-bottom: 24rpx;
		color: #667eea;
	}
	
	/* Control Button Container */
	.control-buttons {
		display: flex;
		gap: 24rpx;
		width: 100%;
	}
	
	/* Button Gradient Styles */
	.control-btn {
		flex: 1;
		border-radius: 24rpx;
		border: none;
		color: white;
		padding: 28rpx 48rpx;
		font-weight: 600;
		font-size: 30rpx;
		transition: all 0.3s ease;
		display: flex;
		align-items: center;
		justify-content: center;
		text-align: center;
	}
	
	.on-btn {
		background: linear-gradient(135deg, #4ade80 0%, #22c55e 100%);
	}
	
	.off-btn {
		background: linear-gradient(135deg, #f87171 0%, #ef4444 100%);
	}
	
	.config-btn {
		background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
		color: white;
		border: none;
		border-radius: 20rpx;
		padding: 20rpx 40rpx;
		font-size: 28rpx;
		font-weight: 600;
		margin-top: 24rpx;
		display: flex;
		align-items: center;
		justify-content: center;
		text-align: center;
		width: 100%;
	}
	
	/* Mode Selector Container */
	.mode-selector {
		display: flex;
		gap: 24rpx;
		width: 100%;
	}
	
	/* Mode Button */
	.mode-btn {
		flex: 1;
		padding: 40rpx 32rpx;
		border-radius: 24rpx;
		background: #f5f5f5;
		display: flex;
		flex-direction: column;
		align-items: center;
		justify-content: center;
		transition: all 0.3s ease;
		min-height: 160rpx;
	}
	
	.mode-btn.active {
		background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
	}
	
	.mode-btn.active .mode-text {
		color: white;
	}
	
	.mode-icon {
		font-size: 48rpx;
		line-height: 1;
		margin-bottom: 12rpx;
		display: block;
	}
	
	.mode-text {
		font-size: 28rpx;
		color: #333;
		font-weight: 600;
		display: block;
		text-align: center;
	}
	
	/* Light Progress Bar */
	.light-bar {
		height: 24rpx;
		background: #e5e7eb;
		border-radius: 12rpx;
		overflow: hidden;
	}
	
	.light-fill {
		height: 100%;
		background: linear-gradient(90deg, #fbbf24 0%, #f59e0b 100%);
		border-radius: 12rpx;
		display: flex;
		align-items: center;
		justify-content: flex-end;
		padding-right: 16rpx;
		transition: width 0.5s ease;
	}
	
	.light-text {
		color: white;
		font-size: 20rpx;
		font-weight: 600;
	}
	
	/* Color Picker Container */
	.color-presets {
		display: flex;
		gap: 20rpx;
		justify-content: space-around;
		flex-wrap: wrap;
	}
	
	/* Color Picker */
	.preset-color {
		width: 100rpx;
		height: 100rpx;
		border-radius: 50%;
		border: 4rpx solid transparent;
		display: flex;
		align-items: center;
		justify-content: center;
		transition: all 0.3s ease;
		box-shadow: 0 4rpx 12rpx rgba(0, 0, 0, 0.15);
	}
	
	.color-label {
		font-size: 28rpx;
		font-weight: 600;
	}
	
	/* Effect Button Container */
	.effect-buttons {
		display: flex;
		gap: 16rpx;
		flex-wrap: wrap;
		justify-content: space-between;
	}
	
	/* Effect Button */
	.effect-btn {
		background: linear-gradient(135deg, #60a5fa 0%, #3b82f6 100%);
		color: white;
		border: none;
		border-radius: 20rpx;
		padding: 24rpx 32rpx;
		font-size: 28rpx;
		font-weight: 600;
		flex: 0 0 calc(50% - 8rpx);
		display: flex;
		align-items: center;
		justify-content: center;
		text-align: center;
	}
	
	/* Status Text Color */
	.status-text.success {
		color: #22c55e;
		font-weight: 600;
	}
	
	.status-text.error {
		color: #ef4444;
		font-weight: 600;
	}
	
	.status-text.warning {
		color: #f59e0b;
		font-weight: 600;
	}
	
	/* Container */
	.container {
		background: #f5f5f5;
		min-height: 100vh;
	}
	
	/* Header Title */
	.header {
		background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
		padding: 48rpx 32rpx;
		text-align: center;
	}
	
	.title {
		font-size: 48rpx;
		font-weight: 700;
		color: white;
		margin-bottom: 8rpx;
	}
	
	.subtitle {
		font-size: 28rpx;
		color: rgba(255, 255, 255, 0.9);
	}
	
	/* Refresh Info */
	.refresh-info {
		text-align: center;
		padding: 24rpx;
		color: #6b7280;
		font-size: 24rpx;
	}
	
	.update-time {
		color: #9ca3af;
		font-size: 22rpx;
		margin-left: 16rpx;
	}
	
	/* Brightness Control Container */
	.brightness-control {
		display: flex;
		flex-direction: column;
		gap: 16rpx;
	}
	
	.brightness-label {
		font-size: 28rpx;
		color: #333;
		font-weight: 600;
		text-align: center;
	}
	
	/* Status Row */
	.status-row {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 16rpx 0;
		border-bottom: 1rpx solid #f0f0f0;
	}
	
	.status-row:last-child {
		border-bottom: none;
	}
	
	.status-label {
		font-size: 28rpx;
		color: #666;
	}
	
	.status-value {
		font-size: 28rpx;
		color: #333;
		font-weight: 600;
	}
	
	.status-value-box {
		display: flex;
		align-items: center;
		gap: 8rpx;
	}
</style>